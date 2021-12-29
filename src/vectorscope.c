#include <obs-module.h>
#include <graphics/image-file.h>
#include <util/platform.h>
#include <graphics/matrix4.h>
#include "plugin-macros.generated.h"
#include "obs-convenience.h"
#include "common.h"
#include "roi.h"

#define debug(format, ...)

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_render_name = "vss_render";
static const char *prof_draw_vectorscope_name = "draw_vectorscope";
static const char *prof_draw_name = "draw";
static const char *prof_draw_graticule_name = "graticule";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

#define VS_SIZE 256
#define N_GRATICULES 18
#define GRATICULES_IQ 256
#define GRATICULES_COLOR_MASK 3
#define SKIN_TONE_LINE 0x0054FF // BGR

extern gs_effect_t *cm_rgb2yuv_effect;

#define RGB2Y_601(r, g, b) ((+306*(r) +601*(g) +117*(b))/1024 +  0)
#define RGB2U_601(r, g, b) ((-150*(r) -296*(g) +448*(b))/1024 +128)
#define RGB2V_601(r, g, b) ((+448*(r) -374*(g) - 72*(b))/1024 +128)

#define RGB2Y_709(r, g, b) ((+218*(r) +732*(g) + 74*(b))/1024 + 16)
#define RGB2U_709(r, g, b) ((-102*(r) -346*(g) +450*(b))/1024 +128)
#define RGB2V_709(r, g, b) ((+450*(r) -408*(g) - 40*(b))/1024 +128)

struct vss_source
{
	struct cm_source cm;

	gs_texture_t *tex_vs;
	uint8_t *tex_buf;

	gs_image_file_t graticule_img;
	gs_vertbuffer_t *graticule_vbuf;
	gs_vertbuffer_t *graticule_line_vbuf;

	int intensity;
	int graticule;
	int graticule_color;
	int graticule_skintone_color;
	int colorspace;
	bool update_graticule;

	float zoom;
};

static const char *vss_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Vectorscope");
}

static void vss_update(void *, obs_data_t *);

static void *vss_create(obs_data_t *settings, obs_source_t *source)
{
	struct vss_source *src = bzalloc(sizeof(struct vss_source));

	src->cm.flags = CM_FLAG_CONVERT_UV;
	src->zoom = 1.0f;
	cm_create(&src->cm, settings, source);

	{
		// The file is generated by
		// inkscape --export-png=data/vectorscope-graticule.png --export-area-page src/vectorscope-graticule.svg
		char *f = obs_module_file("vectorscope-graticule.png");
		gs_image_file_init(&src->graticule_img, f);
		if (!src->graticule_img.loaded)
			blog(LOG_ERROR, "Cannot load '%s'", f);
		obs_enter_graphics();
		gs_image_file_init_texture(&src->graticule_img);
		obs_leave_graphics();
		bfree(f);
	}

	vss_update(src, settings);

	return src;
}

static void vss_destroy(void *data)
{
	struct vss_source *src = data;

	obs_enter_graphics();
	gs_texture_destroy(src->tex_vs);
	bfree(src->tex_buf);
	gs_image_file_free(&src->graticule_img);
	gs_vertexbuffer_destroy(src->graticule_vbuf);
	gs_vertexbuffer_destroy(src->graticule_line_vbuf);
	obs_leave_graphics();

	cm_destroy(&src->cm);
	bfree(src);
}

static void vss_update(void *data, obs_data_t *settings)
{
	struct vss_source *src = data;
	cm_update(&src->cm, settings);

	src->intensity = (int)obs_data_get_int(settings, "intensity");
	if (src->intensity<1)
		src->intensity = 1;

	int graticule = (int)obs_data_get_int(settings, "graticule");
	if ((graticule^src->graticule) & GRATICULES_IQ)
		src->update_graticule = 1;
	src->graticule = graticule;
	switch(graticule & GRATICULES_COLOR_MASK) {
		case 1: src->graticule_color = 0x80FFBF00; break; // amber
		case 2: src->graticule_color = 0x8000FF00; break; // green
	}

	int graticule_skintone_color = (int)obs_data_get_int(settings, "graticule_skintone_color") & 0xFFFFFF;
	if (graticule_skintone_color!=src->graticule_skintone_color) {
		src->graticule_skintone_color = graticule_skintone_color;
		src->update_graticule = 1;
	}

	int colorspace = (int)obs_data_get_int(settings, "colorspace");
	if (colorspace!=src->colorspace) {
		src->colorspace = colorspace;
		src->update_graticule = 1;
	}
}

static void vss_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "target_scale", 2);
	obs_data_set_default_int(settings, "intensity", 25);
	obs_data_set_default_int(settings, "graticule", 1 | GRATICULES_IQ);
	obs_data_set_default_int(settings, "graticule_skintone_color", SKIN_TONE_LINE);
}

static obs_properties_t *vss_get_properties(void *data)
{
	struct vss_source *src = data;
	obs_properties_t *props;
	obs_property_t *prop;
	props = obs_properties_create();

	cm_get_properties(&src->cm, props);

	obs_properties_add_int(props, "intensity", obs_module_text("Intensity"), 1, 255, 1);
	prop = obs_properties_add_list(props, "graticule", obs_module_text("Graticule"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, "None", 0);
	obs_property_list_add_int(prop, "Amber", 1);
	obs_property_list_add_int(prop, "Amber, IQ", 1 + GRATICULES_IQ);
	obs_property_list_add_int(prop, "Green", 2);
	obs_property_list_add_int(prop, "Green, IQ", 2 + GRATICULES_IQ);

	obs_properties_add_color(props, "graticule_skintone_color", obs_module_text("Skin tone color"));

	prop = obs_properties_add_list(props, "colorspace", obs_module_text("Color space"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, "Auto", 0);
	obs_property_list_add_int(prop, "601", 1);
	obs_property_list_add_int(prop, "709", 2);

	return props;
}

static uint32_t vss_get_width(void *data)
{
	struct vss_source *src = data;
	return src->cm.bypass ? cm_get_width(&src->cm) : VS_SIZE;
}

static uint32_t vss_get_height(void *data)
{
	struct vss_source *src = data;
	return src->cm.bypass ? cm_get_height(&src->cm) : VS_SIZE;
}

static inline void vss_draw_vectorscope(struct vss_source *src, uint8_t *video_data, uint32_t video_line)
{
	if (!src->tex_buf)
		src->tex_buf = bzalloc(VS_SIZE*VS_SIZE);
	uint8_t *dbuf = src->tex_buf;

	for (int i=0; i<VS_SIZE*VS_SIZE; i++)
		dbuf[i] = 0;

	const uint32_t height = src->cm.known_height;
	const uint32_t width = src->cm.known_width;
	uint8_t *vd = video_data;
	uint32_t vd_add = video_line - width*4;
	for (uint32_t y=0; y<height; y++) {
		for (uint32_t x=0; x<width; x++) {
			const uint8_t u = *vd++;
			const uint8_t b = *vd++;
			const uint8_t v = *vd++;
			const uint8_t a = *vd++;
			uint8_t *c = dbuf + (u + VS_SIZE*(255-v));
			if (*c<255) ++*c;
		}
		vd += vd_add;
	}

	if (!src->tex_vs)
		src->tex_vs = gs_texture_create(VS_SIZE, VS_SIZE, GS_R8, 1, (const uint8_t**)&src->tex_buf, GS_DYNAMIC);
	else
		gs_texture_set_image(src->tex_vs, src->tex_buf, VS_SIZE, false);
}

static void create_graticule_vbuf(struct vss_source *src)
{
	if (src->graticule_vbuf)
		return;

	obs_enter_graphics();
	src->graticule_vbuf = create_uv_vbuffer(N_GRATICULES*6, false);
	struct gs_vb_data *vdata = gs_vertexbuffer_get_data(src->graticule_vbuf);
	struct vec2 *tvarray = (struct vec2 *)vdata->tvarray[0].array;
	// copied from FFmpeg vectorscope filter
	const float pp[2][12][2] = {
		{ // 601
			{  90, 240 }, { 240, 110 }, { 166,  16 },
			{  16, 146 }, {  54,  34 }, { 202, 222 },
			{  44, 142 }, { 156,  44 }, {  72,  58 },
			{ 184, 198 }, { 100, 212 }, { 212, 114 },
		},
		{ // 709
			{ 102, 240 }, { 240, 118 }, { 154,  16 },
			{  16, 138 }, {  42,  26 }, { 214, 230 },
			{ 212, 120 }, { 109, 212 }, { 193, 204 },
			{  63,  52 }, { 147,  44 }, {  44, 136 },
		},
	};
	const int ppi = src->cm.colorspace-1;

	// label
	for (int i=0; i<6; i++) {
		float x = pp[ppi][i][0];
		float y = 256.f-pp[ppi][i][1];
		if      (x <  72) y += 20;
		else if (x > 184) y -= 20;
		else if (y > 128) x += 20;
		else              x -= 20;
		set_v3_rect(vdata->points + i*6, x-8, y-8, 16, 16);
		set_v2_uv(tvarray + i*6, i/6.f, 0.f, (i+1)/6.f, 1.f);
	}

	// box
	gs_vertexbuffer_destroy(src->graticule_line_vbuf);
	src->graticule_line_vbuf = NULL;
	gs_render_start(true);
	for (int i=0; i<12; i++) {
		const float x = pp[ppi][i][0];
		const float y = 256.f-pp[ppi][i][1];
		const float box[16][2] = {
			{ -6, -6 }, { -2, -6 },
			{ -6, -6 }, { -6, -2 },
			{ +6, -6 }, { +2, -6 },
			{ +6, -6 }, { +6, -2 },
			{ -6, +6 }, { -2, +6 },
			{ -6, +6 }, { -6, +2 },
			{ +6, +6 }, { +2, +6 },
			{ +6, +6 }, { +6, +2 },
		};
		for (int j=0; j<16; j++)
			gs_vertex2f(x+box[j][0], y+box[j][1]);
	}

	// skin tone line
	float stl_u, stl_v, stl_norm;
	int stl_b = src->graticule_skintone_color >> 16 & 0xFF;
	int stl_g = src->graticule_skintone_color >> 8 & 0xFF;
	int stl_r = src->graticule_skintone_color & 0xFF;
	switch(src->cm.colorspace) {
		case 1: // BT.601
			stl_u = (float)RGB2U_601(stl_r, stl_g, stl_b);
			stl_v = (float)RGB2V_601(stl_r, stl_g, stl_b);
			break;
		default: // BT.709
			stl_u = (float)RGB2U_709(stl_r, stl_g, stl_b);
			stl_v = (float)RGB2V_709(stl_r, stl_g, stl_b);
			break;
	}
	stl_norm = hypotf(stl_u-128.0f, stl_v-128.0f);
	if (stl_norm > 1.0f) {
		stl_u = (stl_u-128.0f) * 128.f/stl_norm + 128.0f;
		stl_v = (stl_v-128.0f) * 128.f/stl_norm + 128.0f;
		if (src->graticule & GRATICULES_IQ) {
			gs_vertex2f(255.f-stl_u, stl_v);
			gs_vertex2f(stl_u, 255.f-stl_v);
			gs_vertex2f(stl_v, stl_u);
			gs_vertex2f(255.f-stl_v, 255.f-stl_u);
		}
		else {
			gs_vertex2f(127.5f, 127.5f);
			gs_vertex2f(stl_u, 255.f-stl_v);
		}
	}

	// boxes and skin tone line
	src->graticule_line_vbuf = gs_render_save();

	obs_leave_graphics();
}

static void vss_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct vss_source *src = data;
	if (src->cm.bypass) {
		cm_render_bypass(&src->cm);
		return;
	}

	PROFILE_START(prof_render_name);

	if (src->update_graticule || src->cm.colorspace<1) {
		src->cm.colorspace = calc_colorspace(src->colorspace);
		// TODO: how to set the same colorspace for ROI and all referred sources?
		if (src->cm.target && src->cm.roi)
			src->cm.roi->cm.colorspace = src->cm.colorspace;
		src->update_graticule = 0;
		gs_vertexbuffer_destroy(src->graticule_vbuf);
		src->graticule_vbuf = NULL;
		gs_vertexbuffer_destroy(src->graticule_line_vbuf);
		src->graticule_line_vbuf = NULL;
	}

	bool updated = cm_render_target(&src->cm);

	if (updated) {
		uint8_t *video_data = NULL;
		uint32_t video_linesize;
		PROFILE_START(prof_draw_vectorscope_name);
		if (cm_stagesurface_map(&src->cm, &video_data, &video_linesize)) {
			vss_draw_vectorscope(src, video_data, video_linesize);
			cm_stagesurface_unmap(&src->cm);
		}
		PROFILE_END(prof_draw_vectorscope_name);
	}

	const bool b_zoom = src->zoom > 1.01f;
	if (b_zoom) {
		float offset = 127.5f * (1.0f - src->zoom);
		struct matrix4 tr = {
			{ src->zoom, 0.0f, 0.0f, 0.0f },
			{ 0.0f, src->zoom, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f, 0.0f },
			{ offset, offset, 0.0f, 1.0f, } // TODO: add offset here
		};
		gs_matrix_push();
		gs_matrix_mul(&tr);
	}

	PROFILE_START(prof_draw_name);
	if (src->tex_vs) {
		gs_effect_t *effect = cm_rgb2yuv_effect ? cm_rgb2yuv_effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), src->tex_vs);
		gs_effect_set_float(gs_effect_get_param_by_name(effect, "intensity"), (float)src->intensity);
		gs_effect_set_default(gs_effect_get_param_by_name(effect, "color"));
		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite(src->tex_vs, 0, VS_SIZE, VS_SIZE);
		}
	}
	PROFILE_END(prof_draw_name);

	PROFILE_START(prof_draw_graticule_name);
	if (src->graticule_img.loaded && src->graticule) {
		create_graticule_vbuf(src);
		gs_effect_t *effect = cm_rgb2yuv_effect ? cm_rgb2yuv_effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_effect_set_color(gs_effect_get_param_by_name(effect, "color"), src->graticule_color);
		draw_uv_vbuffer(src->graticule_vbuf, src->graticule_img.texture, effect, "DrawGraticule", N_GRATICULES*2);
	}

	if (src->graticule && src->graticule_line_vbuf) {
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
		gs_effect_set_color(gs_effect_get_param_by_name(effect, "color"), src->graticule_color);
		gs_load_vertexbuffer(src->graticule_line_vbuf);
		while (gs_effect_loop(effect, "Solid")) {
			gs_draw(GS_LINES, 0, 0);
		}
	}
	PROFILE_END(prof_draw_graticule_name);

	if (b_zoom) {
		gs_matrix_pop();
	}

	PROFILE_END(prof_render_name);
}

void vss_mouse_wheel(void *data, const struct obs_mouse_event *event, int x_delta, int y_delta)
{
	struct vss_source *src = data;

	src->zoom *= expf(y_delta * 5e-4f);
	if (src->zoom < 1.0f)
		src->zoom = 1.0f;
}

struct obs_source_info colormonitor_vectorscope = {
	.id = "vectorscope_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION,
	.get_name = vss_get_name,
	.create = vss_create,
	.destroy = vss_destroy,
	.update = vss_update,
	.get_defaults = vss_get_defaults,
	.get_properties = vss_get_properties,
	.get_width = vss_get_width,
	.get_height = vss_get_height,
	.enum_active_sources = cm_enum_sources,
	.video_render = vss_render,
	.video_tick = cm_tick,
	.mouse_wheel = vss_mouse_wheel,
};
