#include <obs-module.h>
#include <util/platform.h>
#include "plugin-macros.generated.h"
#include "common.h"

#define debug(format, ...)

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_render_name = "zebra_render";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

gs_effect_t *zebra_effect;

// common structure for source and filter
struct zb_source
{
	float zebra_th_low, zebra_th_high;
	float zebra_tm;
};

struct zbs_source
{
	struct cm_source cm;
	struct zb_source zb;
};

struct zbf_source
{
	struct zb_source zb;
	obs_source_t *context;
};

static const char *zb_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Zebra");
}

static void zb_update(void *data, obs_data_t *settings);
static void zbs_update(void *, obs_data_t *);

static void zb_init(struct zb_source *src, obs_data_t *settings)
{
	obs_enter_graphics();
	if (!zebra_effect) {
		char *f = obs_module_file("zebra.effect");
		zebra_effect = gs_effect_create_from_file(f, NULL);
		if (!zebra_effect)
			blog(LOG_ERROR, "Cannot load '%s'", f);
		bfree(f);
	}
	obs_leave_graphics();
}

static void *zbs_create(obs_data_t *settings, obs_source_t *source)
{
	struct zbs_source *src = bzalloc(sizeof(struct zbs_source));

	cm_create(&src->cm, settings, source);
	zb_init(&src->zb, settings);

	zbs_update(src, settings);

	return src;
}

static void *zbf_create(obs_data_t *settings, obs_source_t *source)
{
	struct zbf_source *src = bzalloc(sizeof(struct zbf_source));

	zb_init(&src->zb, settings);
	src->context = source;

	zb_update(src, settings);

	return src;
}

static void zbs_destroy(void *data)
{
	struct zbs_source *src = data;

	cm_destroy(&src->cm);
	bfree(src);
}

static void zbf_destroy(void *data)
{
	struct zbf_source *src = data;
	bfree(src);
}

static void zb_update(void *data, obs_data_t *settings)
{
	struct zb_source *src = data;

	src->zebra_th_low = obs_data_get_int(settings, "zebra_th_low") * 1e-2f;
	src->zebra_th_high = obs_data_get_int(settings, "zebra_th_high") * 1e-2f;
}

static void zbs_update(void *data, obs_data_t *settings)
{
	struct zbs_source *src = data;
	cm_update(&src->cm, settings);
	zb_update(&src->zb, settings);
}

static void zb_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "zebra_th_low", 75);
	obs_data_set_default_int(settings, "zebra_th_high", 105);
}

static void zb_get_properties(struct zb_source *src, obs_properties_t *props)
{
	obs_property_t *prop;
	prop = obs_properties_add_int(props, "zebra_th_low", obs_module_text("Threshold (lower)"), 50, 105, 1);
	obs_property_int_set_suffix(prop, "%");
	prop = obs_properties_add_int(props, "zebra_th_high", obs_module_text("Threshold (high)"), 50, 105, 1);
	obs_property_int_set_suffix(prop, "%");
}

static obs_properties_t *zbs_get_properties(void *data)
{
	struct zbs_source *src = data;
	obs_properties_t *props;
	props = obs_properties_create();

	cm_get_properties(&src->cm, props);
	zb_get_properties(&src->zb, props);

	return props;
}

static obs_properties_t *zbf_get_properties(void *data)
{
	struct zbf_source *src = data;
	obs_properties_t *props;
	obs_property_t *prop;
	props = obs_properties_create();

	zb_get_properties(&src->zb, props);

	return props;
}

static uint32_t zbs_get_width(void *data)
{
	struct zbs_source *src = data;
	return src->cm.known_width;
}

static uint32_t zbs_get_height(void *data)
{
	struct zbs_source *src = data;
	return src->cm.known_height;
}

static void zbs_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct zbs_source *src = data;
	if (src->cm.bypass) {
		cm_render_bypass(&src->cm);
		return;
	}

	cm_render_target(&src->cm);

	PROFILE_START(prof_render_name);

	gs_texture_t *tex = gs_texrender_get_texture(src->cm.texrender);
	if (zebra_effect && tex) {
		gs_effect_set_texture(gs_effect_get_param_by_name(zebra_effect, "image"), tex);
		gs_effect_set_float(gs_effect_get_param_by_name(zebra_effect, "zebra_th_low"), src->zb.zebra_th_low);
		gs_effect_set_float(gs_effect_get_param_by_name(zebra_effect, "zebra_th_high"), src->zb.zebra_th_high);
		gs_effect_set_float(gs_effect_get_param_by_name(zebra_effect, "zebra_tm"), src->zb.zebra_tm);
		// TODO: 601
		while (gs_effect_loop(zebra_effect, "DrawZebra709"))
			gs_draw_sprite(tex, 0, src->cm.known_width, src->cm.known_height);
	}

	PROFILE_END(prof_render_name);
}

static void zbf_render(void *data, gs_effect_t *effect)
{
	struct zbf_source *src = data;

	if (!zebra_effect)
		return;

	if (!obs_source_process_filter_begin(src->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	gs_effect_set_float(gs_effect_get_param_by_name(zebra_effect, "zebra_th_low"), src->zb.zebra_th_low);
	gs_effect_set_float(gs_effect_get_param_by_name(zebra_effect, "zebra_th_high"), src->zb.zebra_th_high);
	gs_effect_set_float(gs_effect_get_param_by_name(zebra_effect, "zebra_tm"), src->zb.zebra_tm);

	gs_blend_state_push();
	gs_reset_blend_state();
	// obs_source_process_filter_end(src->context, zebra_effect, 0, 0);
	obs_source_process_filter_tech_end(src->context, zebra_effect, 0, 0, "DrawZebra709");
	gs_blend_state_pop();
}

static void zb_tick(void *data, float seconds)
{
	struct zb_source *src = data;
	src->zebra_tm += seconds * 4.0f;
	if (src->zebra_tm > 12.0f)
		src->zebra_tm -= 12.0f;
}

static void zbs_tick(void *data, float seconds)
{
	struct zbs_source *src = data;
	cm_tick(data, seconds);
	zb_tick(&src->zb, seconds);
}

struct obs_source_info colormonitor_zebra = {
	.id = "zebra_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = zb_get_name,
	.create = zbs_create,
	.destroy = zbs_destroy,
	.update = zbs_update,
	.get_defaults = zb_get_defaults,
	.get_properties = zbs_get_properties,
	.get_width = zbs_get_width,
	.get_height = zbs_get_height,
	.enum_active_sources = cm_enum_sources,
	.video_render = zbs_render,
	.video_tick = zbs_tick,
};

struct obs_source_info colormonitor_zebra_filter = {
	.id = "zebra_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = zb_get_name,
	.create = zbf_create,
	.destroy = zbf_destroy,
	.update = zb_update,
	.get_defaults = zb_get_defaults,
	.get_properties = zbf_get_properties,
	.video_render = zbf_render,
	.video_tick = zb_tick,
};
