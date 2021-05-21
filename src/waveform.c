#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <obs-frontend-api.h>
#include "plugin-macros.generated.h"
#include <graphics/matrix4.h>
#include "common.h"

#define debug(format, ...)

#define WV_SIZE 256
#define SOURCE_CHECK_NS 3000000000

#define DISP_OVERLAY 0
#define DISP_STACK   1
#define DISP_PARADE  2

gs_effect_t *wvs_effect;

struct wvs_source
{
	obs_source_t *self;
	gs_texrender_t *texrender;
	gs_stagesurf_t* stagesurface;
	uint32_t known_width;
	uint32_t known_height;

	gs_texture_t *tex_wv;
	uint8_t *tex_buf;
	uint32_t tex_width;

	pthread_mutex_t target_update_mutex;
	uint64_t target_check_time;
	obs_weak_source_t *weak_target;
	char *target_name;

	gs_vertbuffer_t *graticule_line_vbuf;

	int target_scale;
	int display;
	int intensity;
	int graticule_lines, graticule_lines_prev;
	bool bypass_waveform;

	bool rendered;
	bool enumerating; // not thread safe but I have no other idea.
};

static const char *wvs_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Waveform");
}

static void wvs_update(void *, obs_data_t *);

static void *wvs_create(obs_data_t *settings, obs_source_t *source)
{
	struct wvs_source *src = bzalloc(sizeof(struct wvs_source));

	src->self = source;
	obs_enter_graphics();
	src->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	if (!wvs_effect) {
		char *f = obs_module_file("waveform.effect");
		wvs_effect = gs_effect_create_from_file(f, NULL);
		if (!wvs_effect)
			blog(LOG_ERROR, "Cannot load '%s'", f);
		bfree(f);
	}
	obs_leave_graphics();
	pthread_mutex_init(&src->target_update_mutex, NULL);

	wvs_update(src, settings);

	return src;
}

static void wvs_destroy(void *data)
{
	struct wvs_source *src = data;

	obs_enter_graphics();
	gs_stagesurface_destroy(src->stagesurface);
	gs_texrender_destroy(src->texrender);

	gs_texture_destroy(src->tex_wv);
	bfree(src->tex_buf);
	gs_vertexbuffer_destroy(src->graticule_line_vbuf);
	obs_leave_graphics();

	bfree(src->target_name);
	pthread_mutex_destroy(&src->target_update_mutex);
	obs_weak_source_release(src->weak_target);

	bfree(src);
}

static void wvs_update(void *data, obs_data_t *settings)
{
	struct wvs_source *src = data;
	obs_weak_source_t *weak_source_old = NULL;

	const char *target_name = obs_data_get_string(settings, "target_name");
	if (!src->target_name || strcmp(target_name, src->target_name)) {
		pthread_mutex_lock(&src->target_update_mutex);
		bfree(src->target_name);
		src->target_name = bstrdup(target_name);
		weak_source_old = src->weak_target;
		src->weak_target = NULL;
		src->target_check_time = os_gettime_ns() - SOURCE_CHECK_NS;
		pthread_mutex_unlock(&src->target_update_mutex);
	}

	if (weak_source_old) {
		obs_weak_source_release(weak_source_old);
		weak_source_old = NULL;
	}

	src->target_scale = (int)obs_data_get_int(settings, "target_scale");
	if (src->target_scale<1)
		src->target_scale = 1;

	src->display = (int)obs_data_get_int(settings, "display");

	src->intensity = (int)obs_data_get_int(settings, "intensity");
	if (src->intensity<1)
		src->intensity = 1;

	src->graticule_lines = (int)obs_data_get_int(settings, "graticule_lines");

	src->bypass_waveform = obs_data_get_bool(settings, "bypass_waveform");
}

static void wvs_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "target_scale", 2);
	obs_data_set_default_int(settings, "graticule_lines", 5);
}

static obs_properties_t *wvs_get_properties(void *data)
{
	struct wvs_source *src = data;
	obs_properties_t *props;
	obs_property_t *prop;
	props = obs_properties_create();

	prop = obs_properties_add_list(props, "target_name", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	property_list_add_sources(prop, src ? src->self : NULL);

	obs_properties_add_int(props, "target_scale", obs_module_text("Scale"), 1, 128, 1);
	prop = obs_properties_add_list(props, "display", obs_module_text("Display"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, "Overlay", DISP_OVERLAY);
	obs_property_list_add_int(prop, "Stack",   DISP_STACK);
	obs_property_list_add_int(prop, "Parade",  DISP_PARADE);
	obs_properties_add_int(props, "intensity", obs_module_text("Intensity"), 1, 255, 1);
	prop = obs_properties_add_list(props, "graticule_lines", obs_module_text("Graticule"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, "None", 0);
	obs_property_list_add_int(prop, "0%, 100%", 1);
	obs_property_list_add_int(prop, "0%, 50%, 100%", 2);
	obs_property_list_add_int(prop, "each 25%", 4);
	obs_property_list_add_int(prop, "each 20%", 5);
	obs_property_list_add_int(prop, "each 10%", 10);

	obs_properties_add_bool(props, "bypass_waveform", obs_module_text("Bypass"));

	return props;
}

static uint32_t wvs_get_width(void *data)
{
	struct wvs_source *src = data;
	if (src->bypass_waveform)
		return src->known_width;
	if (src->display==DISP_PARADE)
		return src->known_width*3;
	return src->known_width;
}

static uint32_t wvs_get_height(void *data)
{
	struct wvs_source *src = data;
	if (src->bypass_waveform)
		return src->known_height;
	if (src->display==DISP_STACK)
		return WV_SIZE*3;
	return WV_SIZE;
}

static void wvs_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	struct wvs_source *src = data;
	if (src->enumerating)
		return;
	src->enumerating = 1;
	obs_source_t *target = obs_weak_source_get_source(src->weak_target);
	if (target) {
		enum_callback(src->self, target, param);
		obs_source_release(target);
	}
	src->enumerating = 0;
}

static inline void inc_uint8(uint8_t *c) { if (*c<255) ++*c; }

static inline void wvs_draw_waveform(struct wvs_source *src, uint8_t *video_data, uint32_t video_line)
{
	const uint32_t height = src->known_height;
	const uint32_t width = src->known_width;
	if (width<=0) return;
	if (!src->tex_buf || src->tex_width!=width) {
		bfree(src->tex_buf);
		src->tex_buf = bzalloc(width*WV_SIZE*4);
		src->tex_width = width;
		gs_texture_destroy(src->tex_wv); src->tex_wv = NULL;
	}
	uint8_t *dbuf = src->tex_buf;

	for (uint32_t i=0; i<width*WV_SIZE*4; i++)
		dbuf[i] = 0;

	for (uint32_t y=0; y<height; y++) {
		uint8_t *v = video_data + video_line * y;
		for (uint32_t x=0; x<width; x++) {
			const uint8_t b = *v++;
			const uint8_t g = *v++;
			const uint8_t r = *v++;
			const uint8_t a = *v++;
			if (!a) continue;
			inc_uint8(dbuf + x*4 + (WV_SIZE-1 - b) * width*4 + 0);
			inc_uint8(dbuf + x*4 + (WV_SIZE-1 - g) * width*4 + 1);
			inc_uint8(dbuf + x*4 + (WV_SIZE-1 - r) * width*4 + 2);
		}
	}

	if (!src->tex_wv)
		src->tex_wv = gs_texture_create(width, WV_SIZE, GS_BGRX, 1, (const uint8_t**)&src->tex_buf, GS_DYNAMIC);
	else
		gs_texture_set_image(src->tex_wv, src->tex_buf, width*4, false);
}

static void create_graticule_vbuf(struct wvs_source *src)
{
	obs_enter_graphics();
	gs_vertexbuffer_destroy(src->graticule_line_vbuf);
	src->graticule_line_vbuf = NULL;
	if (src->graticule_lines > 0) {
		gs_render_start(true);
		for (int i=0; i<=src->graticule_lines; i++) {
			gs_vertex2f(0.0f, 255.0f * i / src->graticule_lines);
			gs_vertex2f(1.0f, 255.0f * i / src->graticule_lines);
		}
		src->graticule_line_vbuf = gs_render_save();
	}
	obs_leave_graphics();
}

static void wvs_render_target(struct wvs_source *src)
{
	if (src->rendered)
		return;
	src->rendered = 1;

	gs_texrender_reset(src->texrender);

	obs_source_t *target = obs_weak_source_get_source(src->weak_target);
	if (!target)
		return;

	int target_width = obs_source_get_width(target);
	int target_height = obs_source_get_height(target);
	int width = target_width / src->target_scale;
	int height = target_height / src->target_scale;
	if (width<=0 || height<=0)
		goto end;

	if (gs_texrender_begin(src->texrender, width, height)) {
		struct vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)target_width, 0.0f, (float)target_height, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		obs_source_video_render(target);

		gs_blend_state_pop();

		gs_texrender_end(src->texrender);

		if (width != src->known_width || height != src->known_height) {
			gs_stagesurface_destroy(src->stagesurface);
			src->stagesurface = gs_stagesurface_create(width, height, GS_BGRA);
			src->known_width = width;
			src->known_height = height;
		}

		if (!src->bypass_waveform) {
			gs_stage_texture(src->stagesurface, gs_texrender_get_texture(src->texrender));
			uint8_t *video_data = NULL;
			uint32_t video_linesize;
			if (gs_stagesurface_map(src->stagesurface, &video_data, &video_linesize)) {
				wvs_draw_waveform(src, video_data, video_linesize);
			}
			gs_stagesurface_unmap(src->stagesurface);
		}
	}

end:
	obs_source_release(target);
}

static void wvs_render_graticule(struct wvs_source *src)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_effect_set_color(gs_effect_get_param_by_name(effect, "color"), 0x80FFBF00); // amber
	while (gs_effect_loop(effect, "Solid")) {
		bool stack = src->display==DISP_STACK;
		bool parade = src->display==DISP_PARADE;
		for (int i=0; i<(stack?3:1); i++) {
			struct matrix4 tr = {
				{ parade ? src->known_width*3.0f : src->known_width, 0.0f, 0.0f, 0.0f },
				{ 0.0f, 1.0f, 0.0f, 0.0f },
				{ 0.0f, 0.0f, 1.0f, 0.0f },
				{ 0.0f, stack ? WV_SIZE * i : 0.0f, 0.0f, 1.0f, }
			};
			gs_matrix_push();
			gs_matrix_mul(&tr);
			gs_load_vertexbuffer(src->graticule_line_vbuf);
			gs_draw(GS_LINES, stack && i ? 2 : 0, 0);
			gs_matrix_pop();
		}
	}
}

static void wvs_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct wvs_source *src = data;

	wvs_render_target(src);

	if (src->bypass_waveform) {
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_texture_t *tex = gs_texrender_get_texture(src->texrender);
		if (!tex)
			return;
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite(tex, 0, src->known_width, src->known_height);
		}
		return;
	}

	if (src->tex_wv) {
		gs_effect_t *effect = wvs_effect ? wvs_effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), src->tex_wv);
		gs_effect_set_float(gs_effect_get_param_by_name(effect, "intensity"), (float)src->intensity);
		const char *name = "Draw";
		int w = src->tex_width;
		int h = WV_SIZE;
		if (wvs_effect) switch(src->display) {
			case DISP_STACK:
				name = "DrawStack";
				h *= 3;
				break;
			case DISP_PARADE:
				name = "DrawParade";
				w *= 3;
				break;
			default:
				name = "DrawOverlay";
				break;
		}
		while (gs_effect_loop(effect, name)) {
			gs_draw_sprite(src->tex_wv, 0, w, h);
		}
	}

	if (src->graticule_lines > 0) {
		if (src->graticule_lines != src->graticule_lines_prev) {
			create_graticule_vbuf(src);
			src->graticule_lines_prev = src->graticule_lines;
		}
		wvs_render_graticule(src);
	}
}

static void wvs_tick(void *data, float unused)
{
	UNUSED_PARAMETER(unused);
	struct wvs_source *src = data;

	pthread_mutex_lock(&src->target_update_mutex);
	if (src->target_name && !*src->target_name) {
		obs_source_t *target = obs_frontend_get_current_scene();
		if (src->weak_target)
			obs_weak_source_release(src->weak_target);
		src->weak_target = target ? obs_source_get_weak_source(target) : NULL;
		obs_source_release(target);
	}
	if (is_preview_name(src->target_name)) {
		obs_source_t *target = obs_frontend_get_current_preview_scene();
		if (src->weak_target)
			obs_weak_source_release(src->weak_target);
		src->weak_target = target ? obs_source_get_weak_source(target) : NULL;
		obs_source_release(target);
	}
	else if (src->target_name && *src->target_name && !src->weak_target && src->target_check_time) {
		uint64_t t = os_gettime_ns();
		if (t - src->target_check_time > SOURCE_CHECK_NS) {
			src->target_check_time = t;
			obs_source_t *target = obs_get_source_by_name(src->target_name);
			src->weak_target = target ? obs_source_get_weak_source(target) : NULL;
			obs_source_release(target);
		}
	}
	pthread_mutex_unlock(&src->target_update_mutex);

	src->rendered = 0;
}

struct obs_source_info colormonitor_waveform = {
	.id = "waveform_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = wvs_get_name,
	.create = wvs_create,
	.destroy = wvs_destroy,
	.update = wvs_update,
	.get_defaults = wvs_get_defaults,
	.get_properties = wvs_get_properties,
	.get_width = wvs_get_width,
	.get_height = wvs_get_height,
	.enum_active_sources = wvs_enum_sources,
	.video_render = wvs_render,
	.video_tick = wvs_tick,
};
