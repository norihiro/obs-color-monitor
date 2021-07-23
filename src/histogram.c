#include <obs-module.h>
#include <util/platform.h>
#include "plugin-macros.generated.h"
#include "common.h"

#define debug(format, ...)

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_render_name = "his_render";
static const char *prof_draw_histogram_name = "draw_histogram";
static const char *prof_draw_name = "draw";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

#define HI_SIZE 256

#define DISP_OVERLAY 0
#define DISP_STACK   1
#define DISP_PARADE  2

gs_effect_t *his_effect;

struct his_source
{
	struct cm_source cm;

	gs_texture_t *tex_hi;
	uint8_t *tex_buf;
	uint16_t hi_max[3];

	int display;
	int level_height;
	bool logscale;
};

static const char *his_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Histogram");
}

static void his_update(void *, obs_data_t *);

static void *his_create(obs_data_t *settings, obs_source_t *source)
{
	struct his_source *src = bzalloc(sizeof(struct his_source));

	cm_create(&src->cm, settings, source);
	obs_enter_graphics();
	if (!his_effect) {
		char *f = obs_module_file("histogram.effect");
		his_effect = gs_effect_create_from_file(f, NULL);
		if (!his_effect)
			blog(LOG_ERROR, "Cannot load '%s'", f);
		bfree(f);
	}
	obs_leave_graphics();

	his_update(src, settings);

	return src;
}

static void his_destroy(void *data)
{
	struct his_source *src = data;

	obs_enter_graphics();

	gs_texture_destroy(src->tex_hi);
	bfree(src->tex_buf);
	obs_leave_graphics();

	cm_destroy(&src->cm);
	bfree(src);
}

static void his_update(void *data, obs_data_t *settings)
{
	struct his_source *src = data;
	cm_update(&src->cm, settings);

	src->display = (int)obs_data_get_int(settings, "display");

	src->level_height = (int)obs_data_get_int(settings, "level_height");

	src->logscale = obs_data_get_bool(settings, "logscale");
}

static void his_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "target_scale", 2);
	obs_data_set_default_int(settings, "level_height", 200);
}

static obs_properties_t *his_get_properties(void *data)
{
	struct his_source *src = data;
	obs_properties_t *props;
	obs_property_t *prop;
	props = obs_properties_create();

	cm_get_properties(&src->cm, props);

	prop = obs_properties_add_list(props, "display", obs_module_text("Display"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, "Overlay", DISP_OVERLAY);
	obs_property_list_add_int(prop, "Stack",   DISP_STACK);
	obs_property_list_add_int(prop, "Parade",  DISP_PARADE);
	obs_properties_add_int(props, "level_height", obs_module_text("Height"), 50, 2048, 1);
	obs_properties_add_bool(props, "logscale", obs_module_text("Log scale"));

	return props;
}

static uint32_t his_get_width(void *data)
{
	struct his_source *src = data;
	if (src->cm.bypass)
		return src->cm.known_width;
	if (src->display==DISP_PARADE)
		return HI_SIZE*3;
	return HI_SIZE;
}

static uint32_t his_get_height(void *data)
{
	struct his_source *src = data;
	if (src->cm.bypass)
		return src->cm.known_height;
	if (src->display==DISP_STACK)
		return src->level_height*3;
	return src->level_height;
}

static inline void inc_uint16(uint16_t *c) { if (*c<65535) ++*c; }

static inline void his_draw_histogram(struct his_source *src, uint8_t *video_data, uint32_t video_line)
{
	const uint32_t height = src->cm.known_height;
	const uint32_t width = src->cm.known_width;
	if (width<=0) return;
	if (!src->tex_buf) {
		src->tex_buf = bzalloc(sizeof(uint16_t)*HI_SIZE*4);
	}
	uint16_t *dbuf = (uint16_t*)src->tex_buf;

	for (int i=0; i<HI_SIZE*4; i++)
		dbuf[i] = 0;

	for (uint32_t y=0; y<height; y++) {
		uint8_t *v = video_data + video_line * y;
		for (uint32_t x=0; x<width; x++) {
			const uint8_t b = *v++;
			const uint8_t g = *v++;
			const uint8_t r = *v++;
			const uint8_t a = *v++;
			if (!a) continue;
			inc_uint16(dbuf + r * 4 + 0);
			inc_uint16(dbuf + g * 4 + 1);
			inc_uint16(dbuf + b * 4 + 2);
		}
	}

	src->hi_max[0] = 1;
	src->hi_max[1] = 1;
	src->hi_max[2] = 1;
	for (int i=0; i<HI_SIZE; i++) {
		if (dbuf[i*4+0] > src->hi_max[0]) src->hi_max[0] = dbuf[i*4+0];
		if (dbuf[i*4+1] > src->hi_max[1]) src->hi_max[1] = dbuf[i*4+1];
		if (dbuf[i*4+2] > src->hi_max[2]) src->hi_max[2] = dbuf[i*4+2];
	}

	if (src->logscale) {
		for (int j=0; j<3; j++) {
			const float s = 65535.0f / logf((float)(src->hi_max[j] + 1));
			for (int i=0; i<HI_SIZE; i++)
				dbuf[i*4+j] = dbuf[i*4+j] ? (uint16_t)(logf((float)(dbuf[i*4+j] + 1)) * s) : 0;
			src->hi_max[j] = 65535;
		}
	}

	if (!src->tex_hi)
		src->tex_hi = gs_texture_create(HI_SIZE, 1, GS_RGBA16, 1, (const uint8_t**)&src->tex_buf, GS_DYNAMIC);
	else
		gs_texture_set_image(src->tex_hi, src->tex_buf, sizeof(uint16_t)*HI_SIZE*4, false);
}

static void his_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct his_source *src = data;
	if (src->cm.bypass) {
		cm_render_bypass(&src->cm);
		return;
	}
	PROFILE_START(prof_render_name);

	bool updated = cm_render_target(&src->cm);

	if (updated) {
		uint8_t *video_data = NULL;
		uint32_t video_linesize;
		PROFILE_START(prof_draw_histogram_name);
		if (cm_stagesurface_map(&src->cm, &video_data, &video_linesize)) {
			his_draw_histogram(src, video_data, video_linesize);
			cm_stagesurface_unmap(&src->cm);
		}
		PROFILE_END(prof_draw_histogram_name);
	}

	PROFILE_START(prof_draw_name);
	if (src->tex_hi) {
		gs_effect_t *effect = his_effect ? his_effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), src->tex_hi);
		struct vec3 hi_max; for (int i=0; i<3; i++) hi_max.ptr[i]=(float)src->hi_max[i] / 65535.f;
		gs_effect_set_vec3(gs_effect_get_param_by_name(effect, "hi_max"), &hi_max);
		const char *name = "Draw";
		int w = HI_SIZE;
		int h = src->level_height;
		if (his_effect) switch(src->display) {
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
			gs_draw_sprite(src->tex_hi, 0, w, h);
		}
	}
	PROFILE_END(prof_draw_name);

	PROFILE_END(prof_render_name);
}

struct obs_source_info colormonitor_histogram = {
	.id = "histogram_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = his_get_name,
	.create = his_create,
	.destroy = his_destroy,
	.update = his_update,
	.get_defaults = his_get_defaults,
	.get_properties = his_get_properties,
	.get_width = his_get_width,
	.get_height = his_get_height,
	.enum_active_sources = cm_enum_sources,
	.video_render = his_render,
	.video_tick = cm_tick,
};
