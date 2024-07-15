#include <obs-module.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <graphics/image-file.h>
#include "plugin-macros.generated.h"
#include "common.h"
#include "util.h"

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_render_name = "zebra_render";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

// common structure for source and filter
struct zb_source
{
	gs_effect_t *effect;
	float zebra_th_low, zebra_th_high;
	float zebra_tm;
	char *falsecolor_lut_filename;
	gs_image_file_t falsecolor_lut;
	bool is_falsecolor;
};

struct zbs_source
{
	struct cm_source cm;
	struct zb_source zb;
};

struct zbf_source
{
	struct zb_source zb;
	int colorspace;
	obs_source_t *context;
};

static const char *zb_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Zebra");
}

static const char *fc_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("False Color");
}

static void zbs_update(void *, obs_data_t *);
static void zbf_update(void *, obs_data_t *);

static void zb_init(struct zb_source *src)
{
	obs_enter_graphics();
	src->effect = create_effect_from_module_file(src->is_falsecolor ? "falsecolor.effect" : "zebra.effect");
	obs_leave_graphics();
}

static void *zbs_create(obs_data_t *settings, obs_source_t *source)
{
	struct zbs_source *src = bzalloc(sizeof(struct zbs_source));

	src->cm.flags = CM_FLAG_RAW_TEXTURE;
	cm_create(&src->cm, settings, source);
	zb_init(&src->zb);

	zbs_update(src, settings);

	return src;
}

static void *zbf_create(obs_data_t *settings, obs_source_t *source)
{
	struct zbf_source *src = bzalloc(sizeof(struct zbf_source));

	zb_init(&src->zb);
	src->context = source;

	zbf_update(src, settings);

	return src;
}

static void *fcs_create(obs_data_t *settings, obs_source_t *source)
{
	struct zbs_source *src = bzalloc(sizeof(struct zbs_source));
	src->zb.is_falsecolor = true;

	src->cm.flags = CM_FLAG_RAW_TEXTURE;
	cm_create(&src->cm, settings, source);
	zb_init(&src->zb);

	zbs_update(src, settings);

	return src;
}

static void *fcf_create(obs_data_t *settings, obs_source_t *source)
{
	struct zbf_source *src = bzalloc(sizeof(struct zbf_source));
	src->zb.is_falsecolor = true;

	zb_init(&src->zb);
	src->context = source;

	zbf_update(src, settings);

	return src;
}

static void falsecolor_lut_unload(struct zb_source *src)
{
	if (!src->falsecolor_lut.loaded)
		return;
	obs_enter_graphics();
	gs_image_file_free(&src->falsecolor_lut);
	obs_leave_graphics();
}

static void zb_destroy(struct zb_source *src)
{
	if (src->is_falsecolor) {
		falsecolor_lut_unload(src);
		bfree(src->falsecolor_lut_filename);
	}
}

static void zbs_destroy(void *data)
{
	struct zbs_source *src = data;

	cm_destroy(&src->cm);
	zb_destroy(&src->zb);
	bfree(src);
}

static void zbf_destroy(void *data)
{
	struct zbf_source *src = data;
	zb_destroy(&src->zb);
	bfree(src);
}

static void falsecolor_lut_load(struct zb_source *src, const char *lut_filename)
{
	falsecolor_lut_unload(src);

	blog(LOG_INFO, "Loading LUT file '%s'...", lut_filename);
	gs_image_file_init(&src->falsecolor_lut, lut_filename);

	obs_enter_graphics();
	gs_image_file_init_texture(&src->falsecolor_lut);
	obs_leave_graphics();
}

static void zb_update(struct zb_source *src, obs_data_t *settings)
{
	if (!src->is_falsecolor) {
		/* zebra */
		src->zebra_th_low = obs_data_get_int(settings, "zebra_th_low") * 1e-2f;
		src->zebra_th_high = obs_data_get_int(settings, "zebra_th_high") * 1e-2f;
	} else {
		/* falsecolor */
		bool lut = obs_data_get_bool(settings, "falsecolor_lut");
		const char *lut_filename = lut ? obs_data_get_string(settings, "falsecolor_lut_filename") : NULL;
		if (!lut_filename) {
			falsecolor_lut_unload(src);
			bfree(src->falsecolor_lut_filename);
			src->falsecolor_lut_filename = NULL;
		} else if (!src->falsecolor_lut_filename || strcmp(lut_filename, src->falsecolor_lut_filename) != 0) {
			falsecolor_lut_load(src, lut_filename);
			bfree(src->falsecolor_lut_filename);
			src->falsecolor_lut_filename = bstrdup(lut_filename);
		}
	}
}

static void zbs_update(void *data, obs_data_t *settings)
{
	struct zbs_source *src = data;
	cm_update(&src->cm, settings);
	zb_update(&src->zb, settings);
}

static void zbf_update(void *data, obs_data_t *settings)
{
	struct zbf_source *src = data;
	zb_update(&src->zb, settings);

	int colorspace = (int)obs_data_get_int(settings, "colorspace");
	src->colorspace = calc_colorspace(colorspace);
}

static void zb_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "zebra_th_low", 75);
	obs_data_set_default_int(settings, "zebra_th_high", 100);
}

static void zb_get_properties(obs_properties_t *props, bool is_falsecolor)
{
	obs_property_t *prop;

	if (!is_falsecolor) {
		/* zebra */
		prop = obs_properties_add_int(props, "zebra_th_low", obs_module_text("Threshold (lower)"), 50, 100, 1);
		obs_property_int_set_suffix(prop, "%");
		prop = obs_properties_add_int(props, "zebra_th_high", obs_module_text("Threshold (high)"), 50, 100, 1);
		obs_property_int_set_suffix(prop, "%");
	} else {
		/* falsecolor */
		struct dstr filename_filters = {0};
		dstr_copy(&filename_filters, obs_module_text("FalseColor.Prop.LUTFile.Filter.Image"));
		dstr_cat(&filename_filters, " (*.bmp *.jpg *.jpeg *.tga *.gif *.png);;");
		dstr_cat(&filename_filters, obs_module_text("FalseColor.Prop.LUTFile.Filter.All"));
		dstr_cat(&filename_filters, " (*.*)");
		obs_properties_add_bool(props, "falsecolor_lut", obs_module_text("FalseColor.Prop.LUT"));
		obs_properties_add_path(props, "falsecolor_lut_filename", obs_module_text("FalseColor.Prop.LUTFile"),
					OBS_PATH_FILE, filename_filters.array, NULL);
		dstr_free(&filename_filters);
	}

	properties_add_colorspace(props, "colorspace", obs_module_text("Color space"));
}

static obs_properties_t *zbs_get_properties(void *data)
{
	struct zbs_source *src = data;
	obs_properties_t *props;
	props = obs_properties_create();

	cm_get_properties(&src->cm, props);
	zb_get_properties(props, false);

	return props;
}

static obs_properties_t *zbf_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props;
	props = obs_properties_create();

	zb_get_properties(props, false);

	return props;
}

static obs_properties_t *fcs_get_properties(void *data)
{
	struct zbs_source *src = data;
	obs_properties_t *props;
	props = obs_properties_create();

	cm_get_properties(&src->cm, props);
	zb_get_properties(props, true);

	return props;
}

static obs_properties_t *fcf_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props;
	props = obs_properties_create();

	zb_get_properties(props, true);

	return props;
}

static uint32_t zbs_get_width(void *data)
{
	struct zbs_source *src = data;
	return cm_bypass_get_width(&src->cm);
}

static uint32_t zbs_get_height(void *data)
{
	struct zbs_source *src = data;
	return cm_bypass_get_height(&src->cm);
}

const char *draw_name(int colorspace, bool is_falsecolor)
{
	if (colorspace == 1 && is_falsecolor)
		return "DrawFalseColor601";
	else if (colorspace == 1)
		return "DrawZebra601";
	else if (is_falsecolor)
		return "DrawFalseColor709";
	else
		return "DrawZebra709";
}

static void set_effect_params(struct zb_source *src)
{
	gs_effect_t *e = src->effect;

	if (!src->is_falsecolor) {
		/* zebra */
		gs_effect_set_float(gs_effect_get_param_by_name(e, "zebra_th_low"), src->zebra_th_low);
		gs_effect_set_float(gs_effect_get_param_by_name(e, "zebra_th_high"), src->zebra_th_high);
		gs_effect_set_float(gs_effect_get_param_by_name(e, "zebra_tm"), src->zebra_tm);
	} else {
		/* falsecolor */
		gs_texture_t *lut = src->falsecolor_lut.texture;
		gs_effect_set_bool(gs_effect_get_param_by_name(e, "use_lut"), !!lut);
		if (lut)
			gs_effect_set_texture(gs_effect_get_param_by_name(e, "lut"), lut);
	}
}

static void zbs_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct zbs_source *src = data;
	if (src->cm.bypass) {
		cm_bypass_render(&src->cm);
		return;
	}

	cm_render_target(&src->cm);

	PROFILE_START(prof_render_name);

	gs_texture_t *tex = cm_bypass_get_texture(&src->cm);
	gs_effect_t *e = src->zb.effect;
	if (e && tex) {
		uint32_t cx = cm_bypass_get_width(&src->cm);
		uint32_t cy = cm_bypass_get_height(&src->cm);

		gs_effect_set_texture(gs_effect_get_param_by_name(e, "image"), tex);
		set_effect_params(&src->zb);
		const char *draw = draw_name(src->cm.colorspace, src->zb.is_falsecolor);
		while (gs_effect_loop(e, draw))
			gs_draw_sprite_subregion(tex, 0, 0, 0, cx, cy);
	}

	PROFILE_END(prof_render_name);
}

static void zbf_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct zbf_source *src = data;
	gs_effect_t *e = src->zb.effect;

	if (!e)
		return;

	if (!obs_source_process_filter_begin(src->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	set_effect_params(&src->zb);

	gs_blend_state_push();
	gs_reset_blend_state();

	const char *draw = draw_name(src->colorspace, src->zb.is_falsecolor);
	obs_source_process_filter_tech_end(src->context, e, 0, 0, draw);
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

const struct obs_source_info colormonitor_zebra = {
	.id = ID_PREFIX "zebra_source",
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

const struct obs_source_info colormonitor_zebra_filter = {
	.id = ID_PREFIX "zebra_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = zb_get_name,
	.create = zbf_create,
	.destroy = zbf_destroy,
	.update = zbf_update,
	.get_defaults = zb_get_defaults,
	.get_properties = zbf_get_properties,
	.video_render = zbf_render,
	.video_tick = zb_tick,
};

const struct obs_source_info colormonitor_falsecolor = {
	.id = ID_PREFIX "falsecolor_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = fc_get_name,
	.create = fcs_create,
	.destroy = zbs_destroy,
	.update = zbs_update,
	.get_defaults = zb_get_defaults,
	.get_properties = fcs_get_properties,
	.get_width = zbs_get_width,
	.get_height = zbs_get_height,
	.enum_active_sources = cm_enum_sources,
	.video_render = zbs_render,
	.video_tick = zbs_tick,
};

const struct obs_source_info colormonitor_falsecolor_filter = {
	.id = ID_PREFIX "falsecolor_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = fc_get_name,
	.create = fcf_create,
	.destroy = zbf_destroy,
	.update = zbf_update,
	.get_defaults = zb_get_defaults,
	.get_properties = fcf_get_properties,
	.video_render = zbf_render,
	.video_tick = zb_tick,
};
