#include <obs-module.h>
#include <util/platform.h>
#include "plugin-macros.generated.h"
#include <graphics/matrix4.h>
#include "common.h"

#define debug(format, ...)

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_render_name = "wvs_render";
static const char *prof_draw_waveform_name = "draw_waveform";
static const char *prof_draw_name = "draw";
static const char *prof_draw_graticule_name = "graticule";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

#define WV_SIZE 256

#define DISP_OVERLAY 0
#define DISP_STACK   1
#define DISP_PARADE  2

gs_effect_t *wvs_effect;

struct wvs_source
{
	struct cm_source cm;

	gs_texture_t *tex_wv;
	uint8_t *tex_buf;
	uint32_t tex_width;

	gs_vertbuffer_t *graticule_line_vbuf;

	int display;
	int intensity;
	int graticule_lines, graticule_lines_prev;
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

	cm_create(&src->cm, settings, source);

	obs_enter_graphics();
	if (!wvs_effect) {
		char *f = obs_module_file("waveform.effect");
		wvs_effect = gs_effect_create_from_file(f, NULL);
		if (!wvs_effect)
			blog(LOG_ERROR, "Cannot load '%s'", f);
		bfree(f);
	}
	obs_leave_graphics();

	wvs_update(src, settings);

	return src;
}

static void wvs_destroy(void *data)
{
	struct wvs_source *src = data;

	obs_enter_graphics();
	gs_texture_destroy(src->tex_wv);
	bfree(src->tex_buf);
	gs_vertexbuffer_destroy(src->graticule_line_vbuf);
	obs_leave_graphics();

	cm_destroy(&src->cm);
	bfree(src);
}

static void wvs_update(void *data, obs_data_t *settings)
{
	struct wvs_source *src = data;
	cm_update(&src->cm, settings);

	src->display = (int)obs_data_get_int(settings, "display");

	src->intensity = (int)obs_data_get_int(settings, "intensity");
	if (src->intensity<1)
		src->intensity = 1;

	src->graticule_lines = (int)obs_data_get_int(settings, "graticule_lines");
}

static void wvs_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "target_scale", 2);
	obs_data_set_default_int(settings, "intensity", 51);
	obs_data_set_default_int(settings, "graticule_lines", 5);
}

static obs_properties_t *wvs_get_properties(void *data)
{
	struct wvs_source *src = data;
	obs_properties_t *props;
	obs_property_t *prop;
	props = obs_properties_create();

	cm_get_properties(&src->cm, props);

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

	return props;
}

static uint32_t wvs_get_width(void *data)
{
	struct wvs_source *src = data;
	if (src->cm.bypass)
		return src->cm.known_width;
	if (src->display==DISP_PARADE)
		return src->cm.known_width*3;
	return src->cm.known_width;
}

static uint32_t wvs_get_height(void *data)
{
	struct wvs_source *src = data;
	if (src->cm.bypass)
		return src->cm.known_height;
	if (src->display==DISP_STACK)
		return WV_SIZE*3;
	return WV_SIZE;
}

static inline void inc_uint8(uint8_t *c) { if (*c<255) ++*c; }

static inline void wvs_draw_waveform(struct wvs_source *src, uint8_t *video_data, uint32_t video_line)
{
	const uint32_t height = src->cm.known_height;
	const uint32_t width = src->cm.known_width;
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

static void wvs_render_graticule(struct wvs_source *src)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_effect_set_color(gs_effect_get_param_by_name(effect, "color"), 0x80FFBF00); // amber
	while (gs_effect_loop(effect, "Solid")) {
		bool stack = src->display==DISP_STACK;
		bool parade = src->display==DISP_PARADE;
		for (int i=0; i<(stack?3:1); i++) {
			struct matrix4 tr = {
				{ parade ? src->cm.known_width*3.0f : src->cm.known_width, 0.0f, 0.0f, 0.0f },
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
	if (src->cm.bypass) {
		cm_render_bypass(&src->cm);
		return;
	}
	PROFILE_START(prof_render_name);

	bool updated = cm_render_target(&src->cm);

	if (updated) {
		uint8_t *video_data = NULL;
		uint32_t video_linesize;
		PROFILE_START(prof_draw_waveform_name);
		if (gs_stagesurface_map(src->cm.stagesurface, &video_data, &video_linesize)) {
			wvs_draw_waveform(src, video_data, video_linesize);
			gs_stagesurface_unmap(src->cm.stagesurface);
		}
		PROFILE_END(prof_draw_waveform_name);
	}

	PROFILE_START(prof_draw_name);
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
	PROFILE_END(prof_draw_name);

	PROFILE_START(prof_draw_graticule_name);
	if (src->graticule_lines > 0) {
		if (src->graticule_lines != src->graticule_lines_prev) {
			create_graticule_vbuf(src);
			src->graticule_lines_prev = src->graticule_lines;
		}
		wvs_render_graticule(src);
	}
	PROFILE_END(prof_draw_graticule_name);

	PROFILE_END(prof_render_name);
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
	.enum_active_sources = cm_enum_sources,
	.video_render = wvs_render,
	.video_tick = cm_tick,
};
