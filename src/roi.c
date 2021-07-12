#include <obs-module.h>
#include <graphics/image-file.h>
#include <util/platform.h>
#include <limits.h>
#include "plugin-macros.generated.h"
#include "obs-convenience.h"
#include "roi.h"

#define debug(format, ...)

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_render_name = "roi_render";
static const char *prof_convert_yuv_name = "convert_yuv";
static const char *prof_stage_surface_name = "stage_surface";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

// #define ENABLE_ROI_USER // uncomment if you want to use or debug

extern gs_effect_t *cm_rgb2yuv_effect;

static const char *roi_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("ROI");
}

static void roi_update(void *, obs_data_t *);

static void *roi_create(obs_data_t *settings, obs_source_t *source)
{
	struct roi_source *src = bzalloc(sizeof(struct roi_source));

	src->cm.flags = CM_FLAG_ROI;
	cm_create(&src->cm, settings, source);

	src->x0 = -1;
	src->x1 = -1;
	src->y0 = -1;
	src->y1 = -1;

	roi_update(src, settings);
	return src;
}

static void roi_destroy(void *data)
{
	struct roi_source *src = data;

	cm_destroy(&src->cm);
	bfree(src);
}

static void roi_update(void *data, obs_data_t *settings)
{
	struct roi_source *src = data;
	cm_update(&src->cm, settings);
}

static void roi_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "target_scale", 2);
}

static obs_properties_t *roi_get_properties(void *data)
{
	struct roi_source *src = data;
	obs_properties_t *props = obs_properties_create();
	cm_get_properties(&src->cm, props);

	return props;
}

static uint32_t roi_get_width(void *data)
{
	struct roi_source *src = data;
	return src->cm.known_width;
}

static uint32_t roi_get_height(void *data)
{
	struct roi_source *src = data;
	return src->cm.known_height;
}

static void draw_roi_range(const struct roi_source *src, float x0, float y0, float x1, float y1)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_effect_set_color(gs_effect_get_param_by_name(effect, "color"), 0x80000000);
	while (gs_effect_loop(effect, "Solid")) {
		float w = src->cm.known_width;
		float h = src->cm.known_height;
		gs_render_start(false);
		gs_vertex2f(x0, y1);
		gs_vertex2f(0.0f, h);
		gs_vertex2f(x0, y0);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertex2f(x1, y0);
		gs_vertex2f(w, 0.0f);
		gs_vertex2f(x1, y1);
		gs_vertex2f(w, h);
		gs_vertex2f(x0, y1);
		gs_vertex2f(0.0f, h);
		gs_render_stop(GS_TRISTRIP);
	}
}

static inline gs_stagesurf_t *resize_stagesurface(gs_stagesurf_t *stagesurface, int width, int height)
{
	if (
			!stagesurface ||
			width != gs_stagesurface_get_width(stagesurface) ||
			height != gs_stagesurface_get_height(stagesurface) ) {
		gs_stagesurface_destroy(stagesurface);
		stagesurface = gs_stagesurface_create(width, height, GS_BGRA);
	}
	return stagesurface;
}

static void roi_stage_texture(struct roi_source *src)
{
	const bool b_rgb = src->n_rgb > 0;
	const bool b_yuv = (src->n_uv > 0 || src->n_y > 0);

	if (!b_rgb && !b_yuv)
		return;

	const int height0 = src->cm.known_height;
	const int height = height0 * (b_rgb && b_yuv ? 2 : 1);
	const int width = src->cm.known_width;

	src->cm.stagesurface = resize_stagesurface(src->cm.stagesurface, width, height);

	if (b_rgb && !b_yuv) {
		PROFILE_START(prof_stage_surface_name);
		gs_stage_texture(src->cm.stagesurface, gs_texrender_get_texture(src->cm.texrender));
		PROFILE_END(prof_stage_surface_name);
		return;
	}

	gs_texrender_reset(src->cm.texrender_yuv);
	if (!gs_texrender_begin(src->cm.texrender_yuv, width, height)) {
		blog(LOG_ERROR, "colormonitor_roi: gs_texrender_begin failed %p %d %d", src->cm.texrender_yuv, width, height);
		return;
	}

	struct vec4 background;
	vec4_zero(&background);
	gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

	if (b_rgb) {
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_texture_t *tex = gs_texrender_get_texture(src->cm.texrender);
		if (tex) {
			gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
			while (gs_effect_loop(effect, "Draw"))
				gs_draw_sprite(tex, 0, width, height0);
		}

		// YUV texture will be drawn on bottom
		gs_matrix_translate3f(0.0f, (float)height0, 0.0f);
	}

	gs_texture_t *tex = gs_texrender_get_texture(src->cm.texrender);
	if (cm_rgb2yuv_effect && tex) {
		PROFILE_START(prof_convert_yuv_name);
		gs_effect_set_texture(gs_effect_get_param_by_name(cm_rgb2yuv_effect, "image"), tex);
		while (gs_effect_loop(cm_rgb2yuv_effect, src->cm.colorspace==1 ? "ConvertRGB_UV601" : "ConvertRGB_UV709"))
			gs_draw_sprite(tex, 0, width, height0);
		PROFILE_END(prof_convert_yuv_name);
	}

	gs_blend_state_pop();
	gs_texrender_end(src->cm.texrender_yuv);

	PROFILE_START(prof_stage_surface_name);
	gs_stage_texture(src->cm.stagesurface, gs_texrender_get_texture(src->cm.texrender_yuv));
	PROFILE_END(prof_stage_surface_name);
}

static void roi_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct roi_source *src = data;

	PROFILE_START(prof_render_name);

	bool updated = cm_render_target(&src->cm);
	if (updated) {
		if (src->x0<0) src->x0 = 0;
		if (src->x1<0 || src->x1>src->cm.known_width)
			src->x1 = src->cm.known_width;
		if (src->y0<0) src->y0 = 0;
		if (src->y1<0 || src->y1>src->cm.known_height)
			src->y1 = src->cm.known_height;

		roi_stage_texture(src);
	}

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_texture_t *tex = gs_texrender_get_texture(src->cm.texrender);
	if (effect && tex) {
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite(tex, 0, src->cm.known_width, src->cm.known_height);
		}
	}

	draw_roi_range(src, src->x0, src->y0, src->x1, src->y1);

	gs_blend_state_pop();

	PROFILE_END(prof_render_name);
}

static inline int min_int(int a, int b) { return a<b ? a : b; }
static inline int max_int(int a, int b) { return a>b ? a : b; }

static void roi_mouse_click(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count)
{
	struct roi_source *src = data;
	// TODO: implement me

	if (mouse_up) {
		src->x0 = min_int(src->x_start, event->x);
		src->y0 = min_int(src->y_start, event->y);
		src->x1 = max_int(src->x_start, event->x);
		src->y1 = max_int(src->y_start, event->y);
		blog(LOG_INFO, "roi_mouse_click: %d %d %d %d", src->x0, src->y0, src->x1, src->y1);
		src->x_start = INT_MIN;
		src->y_start = INT_MIN;
	}
	else {
		src->x_start = event->x;
		src->y_start = event->y;
	}
}

struct obs_source_info colormonitor_roi = {
	.id = "colormonitor_roi",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags =
#ifndef ENABLE_ROI_USER
		OBS_SOURCE_CAP_DISABLED |
#endif
		OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION,
	.get_name = roi_get_name,
	.create = roi_create,
	.destroy = roi_destroy,
	.update = roi_update,
	.get_defaults = roi_get_defaults,
	.get_properties = roi_get_properties,
	.get_width = roi_get_width,
	.get_height = roi_get_height,
	.enum_active_sources = cm_enum_sources,
	.video_render = roi_render,
	.video_tick = cm_tick,
	.mouse_click = roi_mouse_click,
};
