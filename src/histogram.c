#include <obs-module.h>
#include <util/platform.h>
#include "plugin-macros.generated.h"
#include <graphics/matrix4.h>
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

#define COMP_RGB 0x07
#define COMP_Y   0x20
#define COMP_UV  0x50
#define COMP_YUV (COMP_Y | COMP_UV)

#define LEVEL_MODE_NONE 0
#define LEVEL_MODE_PIXEL 1
#define LEVEL_MODE_RATIO 2

struct his_source
{
	struct cm_source cm;

	gs_effect_t *effect;
	gs_texture_t *tex_hi;
	uint8_t *tex_buf;
	uint16_t hi_max[3];

	gs_vertbuffer_t *graticule_line_vbuf;

	int display;
	uint32_t components;
	int level_height;
	int level_fixed_value;
	int level_ratio_value;
	bool logscale;
	int graticule_vertical_lines;
	bool graticule_need_update;
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
	src->effect = create_effect_from_module_file("histogram.effect");
	obs_leave_graphics();

	his_update(src, settings);

	return src;
}

static void his_destroy(void *data)
{
	struct his_source *src = data;

	obs_enter_graphics();
	gs_texture_destroy(src->tex_hi);
	gs_vertexbuffer_destroy(src->graticule_line_vbuf);
	obs_leave_graphics();

	bfree(src->tex_buf);
	cm_destroy(&src->cm);
	bfree(src);
}

static void his_update(void *data, obs_data_t *settings)
{
	struct his_source *src = data;
	cm_update(&src->cm, settings);

	src->display = (int)obs_data_get_int(settings, "display");

	src->components = (uint32_t)obs_data_get_int(settings, "components");
	src->cm.flags =
		(src->components & COMP_RGB ? CM_FLAG_CONVERT_RGB : 0) |
		(src->components & COMP_Y   ? CM_FLAG_CONVERT_Y   : 0) |
		(src->components & COMP_UV  ? CM_FLAG_CONVERT_UV  : 0) ;

	int colorspace = (int)obs_data_get_int(settings, "colorspace");
	src->cm.colorspace = calc_colorspace(colorspace);

	src->level_height = (int)obs_data_get_int(settings, "level_height");

	src->logscale = obs_data_get_bool(settings, "logscale");

	int level_mode = (int)obs_data_get_int(settings, "level_mode");
	switch(level_mode) {
		case LEVEL_MODE_NONE:
			src->level_ratio_value = 0;
			src->level_fixed_value = 0;
			break;
		case LEVEL_MODE_PIXEL:
			src->level_fixed_value = (int)obs_data_get_int(settings, "level_fixed_value");
			src->level_ratio_value = 0;
			break;
		case LEVEL_MODE_RATIO:
			src->level_ratio_value = (int)(obs_data_get_double(settings, "level_ratio_value") * 10.0 + 0.5);
			src->level_fixed_value = 0;
			break;
		default:
			blog(LOG_ERROR, "histogram '%s': Invalid level_mode %d",
					obs_source_get_name(src->cm.self), level_mode);
	}

	int graticule_vertical_lines = (int)obs_data_get_int(settings, "graticule_vertical_lines");
	if (graticule_vertical_lines != src->graticule_vertical_lines) {
		src->graticule_vertical_lines = graticule_vertical_lines;
		src->graticule_need_update = true;
	}
}

static void his_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "target_scale", 2);
	obs_data_set_default_int(settings, "components", COMP_RGB);
	obs_data_set_default_int(settings, "level_height", 200);
	obs_data_set_default_int(settings, "graticule_vertical_lines", 5);
	obs_data_set_default_int(settings, "level_fixed_value", 1000);
	obs_data_set_default_double(settings, "level_ratio_value", 10.0);
}

static bool components_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	uint32_t components = settings ? (uint32_t)obs_data_get_int(settings, "components") : 0;
	obs_property_t *prop = obs_properties_get(props, "colorspace");
	// TODO: temporarily disable colorspace setting if the target is ROI
	bool vis = !!(components & COMP_YUV);
	if (vis && is_roi_source_name(obs_data_get_string(settings, "target_name")))
		vis = false;
	if (prop)
		obs_property_set_visible(prop, vis);
	return true;
}

static bool level_mode_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	obs_property_t *prop;
	int level_mode = (int)obs_data_get_int(settings, "level_mode");

	prop = obs_properties_get(props, "level_fixed_value");
	obs_property_set_visible(prop, level_mode == LEVEL_MODE_PIXEL);

	prop = obs_properties_get(props, "level_ratio_value");
	obs_property_set_visible(prop, level_mode == LEVEL_MODE_RATIO);

	return true;
}

static obs_properties_t *his_get_properties(void *data)
{
	struct his_source *src = data;
	obs_properties_t *props;
	obs_property_t *prop;
	props = obs_properties_create();

	cm_get_properties(&src->cm, props);

	prop = obs_properties_add_list(props, "display", obs_module_text("Display"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("Overlay"), DISP_OVERLAY);
	obs_property_list_add_int(prop, obs_module_text("Stack"),   DISP_STACK);
	obs_property_list_add_int(prop, obs_module_text("Parade"),  DISP_PARADE);

	prop = obs_properties_add_list(props, "components", obs_module_text("Components"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_modified_callback(prop, components_changed);
	obs_property_list_add_int(prop, obs_module_text("RGB")   , COMP_RGB);
	obs_property_list_add_int(prop, obs_module_text("Luma")  , COMP_Y);
	obs_property_list_add_int(prop, obs_module_text("Chroma"), COMP_UV);
	obs_property_list_add_int(prop, obs_module_text("YUV")   , COMP_YUV);

	// TODO: Disable this property if ROI target is selected.
	prop = obs_properties_add_list(props, "colorspace", obs_module_text("Color space"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("Auto"), 0);
	obs_property_list_add_int(prop, obs_module_text("601"), 1);
	obs_property_list_add_int(prop, obs_module_text("709"), 2);

	obs_properties_add_int(props, "level_height", obs_module_text("Height"), 50, 2048, 1);
	obs_properties_add_bool(props, "logscale", obs_module_text("Log scale"));

	prop = obs_properties_add_list(props, "level_mode", obs_module_text("Level mode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("Auto"), LEVEL_MODE_NONE);
	obs_property_list_add_int(prop, obs_module_text("Pixels"), LEVEL_MODE_PIXEL);
	obs_property_list_add_int(prop, obs_module_text("Ratio"), LEVEL_MODE_RATIO);
	obs_property_set_modified_callback(prop, level_mode_modified);

	prop = obs_properties_add_int(props, "level_fixed_value", obs_module_text("Top level"), 50, 65535, 1);
	obs_property_int_set_suffix(prop, " px");
	prop = obs_properties_add_float(props, "level_ratio_value", obs_module_text("Top level"), 1.0, 100.0, 0.1);
	obs_property_float_set_suffix(prop, "%");

	prop = obs_properties_add_list(props, "graticule_vertical_lines", obs_module_text("Histogram.Graticule.V"),
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("None"), 0);
	obs_property_list_add_int(prop, "0%, 100%", 1);
	obs_property_list_add_int(prop, "0%, 50%, 100%", 2);
	obs_property_list_add_int(prop, "each 25%", 4);
	obs_property_list_add_int(prop, "each 20%", 5);
	obs_property_list_add_int(prop, "each 10%", 10);

	return props;
}

static inline uint32_t n_components(const struct his_source *src)
{
	uint32_t c = src->components & (COMP_RGB | COMP_YUV);
	c = c - ((c >> 1) & 0x55);
	c = (c & 0x33) + ((c >> 2) & 0x33);
	c = (c & 0x0F) + ((c >> 4) & 0x0F);
	return c;
}

static uint32_t his_get_width(void *data)
{
	struct his_source *src = data;
	if (src->cm.bypass)
		return cm_get_width(&src->cm);
	if (src->display==DISP_PARADE)
		return HI_SIZE*n_components(src);
	return HI_SIZE;
}

static uint32_t his_get_height(void *data)
{
	struct his_source *src = data;
	if (src->cm.bypass)
		return cm_get_height(&src->cm);
	if (src->display==DISP_STACK)
		return src->level_height*n_components(src);
	return src->level_height;
}

static inline void inc_uint16(uint16_t *c) { if (*c<65535) ++*c; }

static inline void his_calculate_max(struct his_source *src, const uint16_t *dbuf)
{
	const bool calc_b = (src->components & 0x11) ? true : false;
	const bool calc_g = (src->components & 0x22) ? true : false;
	const bool calc_r = (src->components & 0x44) ? true : false;

	src->hi_max[0] = 1;
	src->hi_max[1] = 1;
	src->hi_max[2] = 1;
	for (int i=0; i<HI_SIZE; i++) {
		if (calc_r && dbuf[i*4+0] > src->hi_max[0]) src->hi_max[0] = dbuf[i*4+0];
		if (calc_g && dbuf[i*4+1] > src->hi_max[1]) src->hi_max[1] = dbuf[i*4+1];
		if (calc_b && dbuf[i*4+2] > src->hi_max[2]) src->hi_max[2] = dbuf[i*4+2];
	}
}

static inline void his_fix_max_level(struct his_source *src, uint32_t x)
{
	uint16_t v = x > UINT16_MAX ? UINT16_MAX : x == 0 ? 1 : x;
	src->hi_max[0] = v;
	src->hi_max[1] = v;
	src->hi_max[2] = v;
}

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

	const bool calc_b = (src->components & 0x11) ? true : false;
	const bool calc_g = (src->components & 0x22) ? true : false;
	const bool calc_r = (src->components & 0x44) ? true : false;

	for (uint32_t y=0; y<height; y++) {
		uint8_t *v = video_data + video_line * y;
		for (uint32_t x=0; x<width; x++) {
			const uint8_t b = *v++;
			const uint8_t g = *v++;
			const uint8_t r = *v++;
			const uint8_t a = *v++;
			if (!a) continue;
			if (calc_r) inc_uint16(dbuf + r * 4 + 0);
			if (calc_g) inc_uint16(dbuf + g * 4 + 1);
			if (calc_b) inc_uint16(dbuf + b * 4 + 2);
		}
	}

	if (src->level_fixed_value > 0)
		his_fix_max_level(src, src->level_fixed_value);
	else if (src->level_ratio_value > 0)
		his_fix_max_level(src, (uint64_t)width * height * src->level_ratio_value / 1000);
	else
		his_calculate_max(src, dbuf);

	if (src->logscale) {
		for (int j=0, mask=0x44; j<3; j++, mask>>=1) {
			if (!(src->components & mask))
				continue;
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

static void create_graticule_vbuf(struct his_source *src)
{
	obs_enter_graphics();

	gs_vertexbuffer_destroy(src->graticule_line_vbuf);
	src->graticule_line_vbuf = NULL;

	if (src->graticule_vertical_lines > 0) {
		gs_render_start(true);
		const int n = src->graticule_vertical_lines;
		for (int i = 0; i <= n; i++) {
			gs_vertex2f(256.0f * i / n, 0.0f);
			gs_vertex2f(256.0f * i / n, 1.0f);
		}
		src->graticule_line_vbuf = gs_render_save();
	}

	obs_leave_graphics();
}

static void his_render_graticule(struct his_source *src)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_effect_set_color(gs_effect_get_param_by_name(effect, "color"), 0x80FFBF00); // amber
	while (gs_effect_loop(effect, "Solid")) {
		bool stack = src->display==DISP_STACK;
		bool parade = src->display==DISP_PARADE;
		int n_parade = parade ? n_components(src) : 1;
		int n_stack = stack ? n_components(src) : 1;
		for (int i=0; i<n_parade; i++) {
			const float ycoe = (float)(src->level_height * n_stack);
			const float xoff = parade ? HI_SIZE * i + 0.0f : 1.0f;
			struct matrix4 tr = {
				{ 1.0f, 0.0f, 0.0f, 0.0f },
				{ 0.0f, ycoe, 0.0f, 0.0f },
				{ 0.0f, 0.0f, 1.0f, 0.0f },
				{ xoff, 0.0f, 0.0f, 1.0f, }
			};
			gs_matrix_push();
			gs_matrix_mul(&tr);
			gs_load_vertexbuffer(src->graticule_line_vbuf);
			gs_draw(GS_LINES, parade && i ? 2 : 0, 0);
			gs_matrix_pop();
		}
	}
}

static inline void render_histogram(struct his_source *src)
{
	gs_effect_t *effect = src->effect ? src->effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), src->tex_hi);
	struct vec3 hi_max; for (int i=0; i<3; i++) hi_max.ptr[i]=(float)src->hi_max[i] / 65535.f;
	gs_effect_set_vec3(gs_effect_get_param_by_name(effect, "hi_max"), &hi_max);
	const char *name = "Draw";
	int w = HI_SIZE;
	int h = src->level_height;
	int n = n_components(src);
	if (src->effect) switch(src->display) {
		case DISP_STACK:
			name = n==3 ? "DrawStack" : n==2 ? "DrawStackUV" : "DrawOverlay";
			h *= n;
			break;
		case DISP_PARADE:
			name = n==3 ? "DrawParade" : n==2 ? "DrawParadeUV" : "DrawOverlay";
			w *= n;
			break;
		default:
			name = "DrawOverlay";
			break;
	}
	while (gs_effect_loop(effect, name)) {
		gs_draw_sprite(src->tex_hi, 0, w, h);
	}
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
	if (src->tex_hi)
		render_histogram(src);
	PROFILE_END(prof_draw_name);

	if (src->graticule_need_update) {
		create_graticule_vbuf(src);
		src->graticule_need_update = false;
	}
	if (src->graticule_line_vbuf)
		his_render_graticule(src);

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
