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
static const char *prof_render_name = "zebra_render";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

enum show_key_e {
	show_key_none = 0,
	show_key_left = 1,
	show_key_right = 2,
	show_key_outside = 3,
	show_key_top = 4,
	show_key_bottom = 5,
	show_key_below = 6,
};

// common structure for source and filter
struct zb_source
{
	gs_effect_t *effect;

	/* properties */
	float zebra_th_low, zebra_th_high;
	float zebra_tm;
	enum show_key_e show_key;
	char *falsecolor_lut_filename;

	/* internal data */
	gs_texture_t *key_tex;
	gs_image_file_t key_label_img;
	gs_vertbuffer_t *key_label_vbuf;
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
	if (src->key_tex) {
		obs_enter_graphics();
		gs_texture_destroy(src->key_tex);
		gs_image_file_free(&src->key_label_img);
		gs_vertexbuffer_destroy(src->key_label_vbuf);
		obs_leave_graphics();
	}

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

	src->show_key = obs_data_get_int(settings, "show_key");
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

		prop = obs_properties_add_list(props, "show_key", obs_module_text("Prop.ShowKey"), OBS_COMBO_TYPE_LIST,
					       OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(prop, obs_module_text("Prop.ShowKey.None"), show_key_none);
		obs_property_list_add_int(prop, obs_module_text("Prop.ShowKey.Left"), show_key_left);
		obs_property_list_add_int(prop, obs_module_text("Prop.ShowKey.Right"), show_key_right);
		obs_property_list_add_int(prop, obs_module_text("Prop.ShowKey.Outside"), show_key_outside);
		obs_property_list_add_int(prop, obs_module_text("Prop.ShowKey.Top"), show_key_top);
		obs_property_list_add_int(prop, obs_module_text("Prop.ShowKey.Bottom"), show_key_bottom);
		obs_property_list_add_int(prop, obs_module_text("Prop.ShowKey.Below"), show_key_below);
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
	uint32_t w = cm_bypass_get_width(&src->cm);
	if (!src->cm.bypass && src->zb.show_key == show_key_outside)
		return w * 11 / 10;
	return w;
}

static uint32_t zbs_get_height(void *data)
{
	struct zbs_source *src = data;
	uint32_t h = cm_bypass_get_height(&src->cm);
	if (!src->cm.bypass && src->zb.show_key == show_key_below)
		return h * 12 / 10;
	return h;
}

static uint32_t fcf_get_width(void *data)
{
	struct zbf_source *src = data;
	obs_source_t *target = obs_filter_get_target(src->context);
	uint32_t w = obs_source_get_base_width(target);
	if (src->zb.show_key == show_key_outside)
		return w * 11 / 10;
	return w;
}

static uint32_t fcf_get_height(void *data)
{
	struct zbf_source *src = data;
	obs_source_t *target = obs_filter_get_target(src->context);
	uint32_t h = obs_source_get_base_height(target);
	if (src->zb.show_key == show_key_below)
		return h * 12 / 10;
	return h;
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

static void zb_create_key_tex(struct zb_source *src)
{
#define N 256
	uint8_t *buf = bmalloc(1 * N * 4);
	for (uint32_t i = 0; i < N; i++) {
		buf[i * 4 + 0] = i * 256 / N;
		buf[i * 4 + 1] = i * 256 / N;
		buf[i * 4 + 2] = i * 256 / N;
		buf[i * 4 + 3] = 0xFF;
	}
	const uint8_t *cbuf = buf;
	src->key_tex = gs_texture_create(N, 1, GS_BGRX, 1, &cbuf, 0);
	bfree(buf);
#undef N

	if (!src->key_label_img.loaded) {
		char *f = obs_module_file("falsecolor-key.png");
		gs_image_file_init(&src->key_label_img, f);
		if (!src->key_label_img.loaded)
			blog(LOG_ERROR, "Cannot load falsecolor-key.png (%s)", f);
		gs_image_file_init_texture(&src->key_label_img);
		bfree(f);
	}
}

static void zb_render_key(struct zb_source *src, const char *draw, uint32_t width, uint32_t height)
{
	struct key_def_s
	{
		/* outer box */
		float x0, y0, x1, y1;
		/* key */
		float xk, yk;
		float cxk, cyk;
		uint32_t bg_color;
		bool is_vertical;
	};
	const struct key_def_s points[] = {
		[show_key_left] =
			{
				.x0 = 0.01f,
				.y0 = 0.1f,
				.x1 = 0.09f,
				.y1 = 0.9f,
				.xk = 0.06f,
				.yk = 0.88f,
				.cxk = 0.025f,
				.cyk = -0.76f / 256,
				.bg_color = 0x80000000,
				.is_vertical = true,
			},
		[show_key_right] =
			{
				.x0 = 0.91f,
				.y0 = 0.1f,
				.x1 = 0.99f,
				.y1 = 0.9f,
				.xk = 0.96f,
				.yk = 0.88f,
				.cxk = 0.025f,
				.cyk = -0.76f / 256,
				.bg_color = 0x80000000,
				.is_vertical = true,
			},
		[show_key_outside] =
			{
				.x0 = 1.00f,
				.y0 = 0.0f,
				.x1 = 1.10f,
				.y1 = 1.0f,
				.xk = 1.06f,
				.yk = 0.95f,
				.cxk = 0.03f,
				.cyk = -0.90f / 256,
				.bg_color = 0xFF000000,
				.is_vertical = true,
			},
		[show_key_top] =
			{
				.x0 = 0.1f,
				.y0 = 0.01f,
				.x1 = 0.9f,
				.y1 = 0.09f,
				.xk = 0.12f,
				.yk = 0.05f,
				.cxk = 0.76f / 256,
				.cyk = -0.025f,
				.bg_color = 0x80000000,
				.is_vertical = false,
			},
		[show_key_bottom] =
			{
				.x0 = 0.1f,
				.y0 = 0.91f,
				.x1 = 0.9f,
				.y1 = 0.99f,
				.xk = 0.12f,
				.yk = 0.95f,
				.cxk = 0.76f / 256,
				.cyk = -0.025f,
				.bg_color = 0x80000000,
				.is_vertical = false,
			},
		[show_key_below] =
			{
				.x0 = 0.0f,
				.y0 = 1.00f,
				.x1 = 1.0f,
				.y1 = 1.20f,
				.xk = 0.05f,
				.yk = 1.08f,
				.cxk = 0.90f / 256,
				.cyk = -0.060f,
				.bg_color = 0xFF000000,
				.is_vertical = false,
			},
	};

	if (src->show_key <= show_key_none || src->show_key >= sizeof(points) / sizeof(*points))
		return;

	const struct key_def_s *def = points + src->show_key;

	if (!src->key_tex)
		zb_create_key_tex(src);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_effect_set_color(gs_effect_get_param_by_name(effect, "color"), def->bg_color);
	while (gs_effect_loop(effect, "Solid")) {
		gs_render_start(false);
		gs_vertex2f(def->x0 * width, def->y0 * height);
		gs_vertex2f(def->x1 * width, def->y0 * height);
		gs_vertex2f(def->x1 * width, def->y1 * height);
		gs_vertex2f(def->x1 * width, def->y1 * height);
		gs_vertex2f(def->x0 * width, def->y1 * height);
		gs_vertex2f(def->x0 * width, def->y0 * height);
		gs_render_stop(GS_TRISTRIP);
	}

	effect = src->effect;
	if (!effect)
		return;
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), src->key_tex);
	set_effect_params(src);

	gs_matrix_push();
	struct matrix4 tran;
	if (def->is_vertical) {
		vec4_set(&tran.x, 0.0f, def->cyk * height, 0.0f, 0.0f);
		vec4_set(&tran.y, def->cxk * width, 0.0f, 0.0f, 0.0f);
	} else {
		vec4_set(&tran.x, def->cxk * width, 0.0f, 0.0f, 0.0f);
		vec4_set(&tran.y, 0.0f, def->cyk * height, 0.0f, 0.0f);
	}
	vec4_set(&tran.z, 0.0f, 0.0f, 1.0f, 0.0f);
	vec4_set(&tran.t, def->xk * width, def->yk * height, 0.0f, 1.0f);
	gs_matrix_mul(&tran);

	while (gs_effect_loop(effect, draw)) {
		gs_draw_sprite(src->key_tex, 0, 0, 0);
	}

	gs_matrix_pop();

	if (!src->key_label_img.loaded)
		return;

	effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!src->key_label_vbuf)
		src->key_label_vbuf = create_uv_vbuffer(11 * 6, false);
	struct gs_vb_data *vdata = gs_vertexbuffer_get_data(src->key_label_vbuf);
	struct vec2 *tvarray = (struct vec2 *)vdata->tvarray[0].array;
	for (uint32_t i = 0; i < 11; i++) {
		float x, y, w, h;
		int img_cx, img_cy;
		if (def->is_vertical) {
			x = width * def->x0;
			y = height * (def->yk + def->cyk * 256 * i / 10);
			w = width * (def->xk - def->x0);
			h = height * fabs(def->cyk * 256) / 10;
			img_cx = src->key_label_img.cx * 55; // cx
			img_cy = src->key_label_img.cy * 2;  // cy * 2 / 55
		} else {
			x = width * (def->xk + def->cxk * 256 * i / 10);
			y = height * def->yk;
			w = width * def->cxk * 256 / 11;
			h = height * (def->y1 - def->yk);
			img_cx = src->key_label_img.cx * 55; // cx
			img_cy = src->key_label_img.cy * 3;  // cy * 3 / 55
		}
		// if w / h > cx / cy
		if (w * img_cy > h * img_cx) {
			float nw = h * img_cx / img_cy;
			if (def->is_vertical)
				x += (w - nw) * 0.5f;
			w = nw;
		} else {
			h = w * img_cy / img_cx;
		}
		if (def->is_vertical)
			y -= h * 0.5f;
		else
			x -= w * 0.5f;
		set_v3_rect(vdata->points + i * 6, x, y, w, h);
		if (def->is_vertical) {
			set_v2_uv(tvarray + i * 6, 0.0f, i / 27.5f, 1.f, (i + 1) / 27.5f);
		} else {
			set_v2_uv(tvarray + i * 6, 0.0f, (22 + i * 3) / 55.f, 1.f, (25 + i * 3) / 55.f);
		}
	}

	draw_uv_vbuffer(src->key_label_vbuf, src->key_label_img.texture, effect, "Draw", 11 * 6);
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

		zb_render_key(&src->zb, draw, cx, cy);
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

	obs_source_t *target = obs_filter_get_target(src->context);
	if (target) {
		uint32_t cx = obs_source_get_base_width(target);
		uint32_t cy = obs_source_get_base_height(target);
		zb_render_key(&src->zb, draw, cx, cy);
	}

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
	.get_width = fcf_get_width,
	.get_height = fcf_get_height,
	.video_render = zbf_render,
	.video_tick = zb_tick,
};
