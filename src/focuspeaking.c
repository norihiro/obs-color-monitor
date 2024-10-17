#include <obs-module.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <graphics/matrix4.h>
#include <graphics/image-file.h>
#include "plugin-macros.generated.h"
#include "obs-convenience.h"
#include "common.h"
#include "util.h"

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_render_name = "focuspeaking_render";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

#define DEFAULT_PEAKING_COLOR 0xFFFF5400 // ABGR
#define DEFAULT_PEAKING_THRESHOLD 0.05

/* common structure for source and filter */
struct fp_source
{
	gs_effect_t *effect;

	/* properties */
	uint32_t peaking_color;
	float peaking_threshold;
	bool actual_size;
};

struct fps_source
{
	struct cm_source cm;
	struct fp_source fp;
};

struct fpf_source
{
	struct fp_source fp;
	obs_source_t *context;
};

static const char *fp_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("FocusPeaking.Name");
}

static void fps_update(void *, obs_data_t *);
static void fpf_update(void *, obs_data_t *);

static void fp_init(struct fp_source *src)
{
	obs_enter_graphics();
	src->effect = create_effect_from_module_file("focuspeaking.effect");
	obs_leave_graphics();
}

static void *fps_create(obs_data_t *settings, obs_source_t *source)
{
	struct fps_source *src = bzalloc(sizeof(struct fps_source));

	src->cm.flags = CM_FLAG_RAW_TEXTURE;
	cm_create(&src->cm, settings, source);
	fp_init(&src->fp);

	fps_update(src, settings);

	return src;
}

static void *fpf_create(obs_data_t *settings, obs_source_t *source)
{
	struct fpf_source *src = bzalloc(sizeof(struct fpf_source));

	fp_init(&src->fp);
	src->context = source;

	fpf_update(src, settings);

	return src;
}

static void fp_destroy(struct fp_source *src)
{
	UNUSED_PARAMETER(src);
}

static void fps_destroy(void *data)
{
	struct fps_source *src = data;

	cm_destroy(&src->cm);
	fp_destroy(&src->fp);
	bfree(src);
}

static void fpf_destroy(void *data)
{
	struct fpf_source *src = data;
	fp_destroy(&src->fp);
	bfree(src);
}

static void fp_update(struct fp_source *src, obs_data_t *settings)
{
	src->peaking_color = (uint32_t)obs_data_get_int(settings, "peaking_color");
	src->peaking_threshold = (float)obs_data_get_double(settings, "peaking_threshold");
	src->actual_size = obs_data_get_bool(settings, "actual_size");
}

static void fps_update(void *data, obs_data_t *settings)
{
	struct fps_source *src = data;
	cm_update(&src->cm, settings);
	fp_update(&src->fp, settings);
}

static void fpf_update(void *data, obs_data_t *settings)
{
	struct fpf_source *src = data;
	fp_update(&src->fp, settings);
}

static void fp_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "peaking_color", DEFAULT_PEAKING_COLOR);
	obs_data_set_default_double(settings, "peaking_threshold", DEFAULT_PEAKING_THRESHOLD);
}

static void fp_get_properties(obs_properties_t *props)
{
	obs_properties_add_color(props, "peaking_color", obs_module_text("FocusPeaking.Prop.PeakingColor"));
	obs_properties_add_float(props, "peaking_threshold", obs_module_text("FocusPeaking.Prop.PeakingThreshold"),
				 0.001, 0.1, 0.001);
	obs_properties_add_bool(props, "actual_size", obs_module_text("FocusPeaking.Prop.ActualSize"));
}

static obs_properties_t *fps_get_properties(void *data)
{
	struct fps_source *src = data;
	obs_properties_t *props;
	props = obs_properties_create();

	cm_get_properties(&src->cm, props);
	fp_get_properties(props);

	return props;
}

static obs_properties_t *fpf_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props;
	props = obs_properties_create();

	fp_get_properties(props);

	return props;
}

static uint32_t fps_get_width(void *data)
{
	struct fps_source *src = data;
	return cm_bypass_get_width(&src->cm);
}

static uint32_t fps_get_height(void *data)
{
	struct fps_source *src = data;
	return cm_bypass_get_height(&src->cm);
}

static uint32_t fpf_get_width(void *data)
{
	struct fpf_source *src = data;
	obs_source_t *target = obs_filter_get_target(src->context);
	return obs_source_get_base_width(target);
}

static uint32_t fpf_get_height(void *data)
{
	struct fpf_source *src = data;
	obs_source_t *target = obs_filter_get_target(src->context);
	return obs_source_get_base_height(target);
}

static const char *draw_name()
{
	return "DrawFocusPeaking";
}

static inline uint32_t swap_rb(uint32_t c)
{
	uint32_t r = c & 0xFF;
	uint32_t b = (c >> 16) & 0xFF;
	return (c & 0xFF00FF00) | (r << 16) | b;
}

static void set_actual_size_matrix(uint32_t cx, uint32_t cy)
{
	struct gs_rect rect;
	gs_get_viewport(&rect);

	float xcoe = (float)cx / (float)rect.cx;
	float ycoe = (float)cy / (float)rect.cy;
	float xoff = (float)(rect.cx - (int)cx) * 0.5f * xcoe;
	float yoff = (float)(rect.cy - (int)cy) * 0.5f * ycoe;

	struct matrix4 tr = {
		{.ptr = {xcoe, 0.0f, 0.0f, 0.0f}},
		{.ptr = {0.0f, ycoe, 0.0f, 0.0f}},
		{.ptr = {0.0f, 0.0f, 1.0f, 0.0f}},
		{.ptr = {xoff, yoff, 0.0f, 1.0f}},
	};
	gs_matrix_mul(&tr);
}

static void set_effect_params(struct fp_source *src, uint32_t cx, uint32_t cy)
{
	gs_effect_t *e = src->effect;

	const struct vec2 dxy = {
		.x = 1.0f / cx,
		.y = 1.0f / cy,
	};

	gs_effect_set_vec2(gs_effect_get_param_by_name(e, "dxy"), &dxy);
	gs_effect_set_color(gs_effect_get_param_by_name(e, "peaking_color"), swap_rb(src->peaking_color));
	gs_effect_set_float(gs_effect_get_param_by_name(e, "peaking_threshold"), src->peaking_threshold);
}

static void fps_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct fps_source *src = data;
	if (src->cm.bypass) {
		cm_bypass_render(&src->cm);
		return;
	}

	cm_render_target(&src->cm);

	PROFILE_START(prof_render_name);

	gs_texture_t *tex = cm_bypass_get_texture(&src->cm);
	gs_effect_t *e = src->fp.effect;
	if (e && tex) {
		uint32_t cx = cm_bypass_get_width(&src->cm);
		uint32_t cy = cm_bypass_get_height(&src->cm);

		if (src->fp.actual_size) {
			gs_matrix_push();
			set_actual_size_matrix(cx, cy);
		}

		gs_effect_set_texture(gs_effect_get_param_by_name(e, "image"), tex);
		set_effect_params(&src->fp, cx, cy);
		const char *draw = draw_name();
		while (gs_effect_loop(e, draw))
			gs_draw_sprite_subregion(tex, 0, 0, 0, cx, cy);

		if (src->fp.actual_size)
			gs_matrix_pop();
	}

	PROFILE_END(prof_render_name);
}

static void fpf_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct fpf_source *src = data;
	gs_effect_t *e = src->fp.effect;

	if (!e)
		return;

	if (!obs_source_process_filter_begin(src->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	obs_source_t *target = obs_filter_get_target(src->context);
	if (!target)
		return;

	uint32_t cx = obs_source_get_base_width(target);
	uint32_t cy = obs_source_get_base_height(target);

	set_effect_params(&src->fp, cx, cy);

	gs_blend_state_push();
	gs_reset_blend_state();

	if (src->fp.actual_size) {
		gs_matrix_push();
		set_actual_size_matrix(cx, cy);
	}

	const char *draw = draw_name();
	obs_source_process_filter_tech_end(src->context, e, 0, 0, draw);

	if (src->fp.actual_size)
		gs_matrix_pop();

	gs_blend_state_pop();
}

const struct obs_source_info colormonitor_focuspeaking = {
	.id = ID_PREFIX "focuspeaking_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = fp_get_name,
	.create = fps_create,
	.destroy = fps_destroy,
	.update = fps_update,
	.get_defaults = fp_get_defaults,
	.get_properties = fps_get_properties,
	.get_width = fps_get_width,
	.get_height = fps_get_height,
	.enum_active_sources = cm_enum_sources,
	.video_render = fps_render,
	.video_tick = cm_tick,
};

const struct obs_source_info colormonitor_focuspeaking_filter = {
	.id = ID_PREFIX "focuspeaking_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = fp_get_name,
	.create = fpf_create,
	.destroy = fpf_destroy,
	.update = fpf_update,
	.get_defaults = fp_get_defaults,
	.get_properties = fpf_get_properties,
	.get_width = fpf_get_width,
	.get_height = fpf_get_height,
	.video_render = fpf_render,
};
