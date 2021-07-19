#include <obs-module.h>
#include <graphics/image-file.h>
#include <util/platform.h>
#include <util/darray.h>
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
static DARRAY(struct roi_source*) da_roi;
static pthread_mutex_t da_roi_mutex;

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
	src->x0in = -1;
	src->x1in = -1;
	src->y0in = -1;
	src->y1in = -1;

	roi_update(src, settings);

	pthread_mutex_lock(&da_roi_mutex);
	da_push_back(da_roi, &src);
	pthread_mutex_unlock(&da_roi_mutex);
	return src;
}

static void roi_destroy(void *data)
{
	struct roi_source *src = data;

	pthread_mutex_lock(&da_roi_mutex);
	da_erase_item(da_roi, &src);
	pthread_mutex_unlock(&da_roi_mutex);

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
	const bool b_rgb = src->b_rgb;
	const bool b_yuv = src->b_yuv;

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

void roi_target_render(struct roi_source *src)
{
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
}

static void roi_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct roi_source *src = data;

	PROFILE_START(prof_render_name);

	roi_target_render(src);

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

bool roi_stagesurfae_map(struct roi_source *src, uint8_t **video_data, uint32_t *video_linesize, int ix)
{
	if (ix && !src->b_yuv) {
		blog(LOG_INFO, "roi_stagesurfae_map: YUV frame is not staged");
		return false;
	}
	if (!ix && !src->b_rgb) {
		blog(LOG_INFO, "roi_stagesurfae_map: RGB frame is not staged");
		return false;
	}
	bool ret = gs_stagesurface_map(src->cm.stagesurface, video_data, video_linesize);
	if (src->x0 > 0)
		*video_data += 4 * src->x0;
	if (src->y0 > 0)
		*video_data += *video_linesize * src->y0;
	if (ix && src->b_rgb && src->b_yuv)
		*video_data += *video_linesize * src->cm.known_height;
	return ret;
}

void roi_stagesurfae_unmap(struct roi_source *src)
{
	gs_stagesurface_unmap(src->cm.stagesurface);
}

static inline int min_int(int a, int b) { return a<b ? a : b; }
static inline int max_int(int a, int b) { return a>b ? a : b; }

static void roi_mouse_click(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count)
{
	struct roi_source *src = data;
	// TODO: implement me

	if (mouse_up) {
		src->x0in = min_int(src->x_start, event->x);
		src->y0in = min_int(src->y_start, event->y);
		src->x1in = max_int(src->x_start, event->x);
		src->y1in = max_int(src->y_start, event->y);
		src->x_start = INT_MIN;
		src->y_start = INT_MIN;
	}
	else {
		src->x_start = event->x;
		src->y_start = event->y;
	}
}

static void roi_tick(void *data, float unused)
{
	cm_tick(data, unused);
	struct roi_source *src = data;

	src->b_rgb = src->n_rgb > 0;
	src->b_yuv = (src->n_uv > 0 || src->n_y > 0);

	if (src->n_rgb > 0)
		src->n_rgb --;
	if (src->n_uv > 0)
		src->n_uv --;
	if (src->n_y > 0)
		src->n_y --;

	src->x0 = src->x0in;
	src->y0 = src->y0in;
	src->x1 = src->x1in;
	src->y1 = src->y1in;
}

struct roi_source *roi_from_source(obs_source_t *s)
{
	struct roi_source *ret = NULL;
	pthread_mutex_lock(&da_roi_mutex);
	for (size_t i=0; i<da_roi.num; i++) {
		if (da_roi.array[i]->cm.self == s) {
			ret = da_roi.array[i];
			break;
		}
	}
	pthread_mutex_unlock(&da_roi_mutex);
	return ret;
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
	.video_tick = roi_tick,
	.mouse_click = roi_mouse_click,
};

void roi_init()
{
	pthread_mutex_init(&da_roi_mutex, NULL);
	da_init(da_roi);
}

void roi_free()
{
	pthread_mutex_lock(&da_roi_mutex);
	if (da_roi.num>0)
		blog(LOG_ERROR, "da_roi has %d element(s)", (int)da_roi.num);
	da_free(da_roi);
	pthread_mutex_unlock(&da_roi_mutex);
	pthread_mutex_destroy(&da_roi_mutex);
}
