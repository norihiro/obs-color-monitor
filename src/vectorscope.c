#include <obs-module.h>
#include <graphics/image-file.h>
#include <util/platform.h>
#include <util/threading.h>
#include "plugin-macros.generated.h"
#include "obs-convenience.h"

#define debug(format, ...)

#define VS_SIZE 256
#define SOURCE_CHECK_NS 3000000000
#define N_GRATICULES 18

gs_effect_t *vss_effect = NULL;

struct vss_source
{
	obs_source_t *self;
	gs_texrender_t *texrender;
	gs_stagesurf_t* stagesurface;
	uint32_t known_width;
	uint32_t known_height;

	gs_texture_t *tex_vs;
	uint8_t *tex_buf;

	pthread_mutex_t target_update_mutex;
	uint64_t target_check_time;
	obs_weak_source_t *weak_target;
	char *target_name;

	gs_image_file_t graticule_img;
	gs_vertbuffer_t *graticule_vbuf;

	int target_scale;
	int intensity;
	int graticule;
	int colorspace;
	int colorspace_calc; // get from ovi if auto
	bool colorspace_updated;
	bool bypass_vectorscope;

	bool rendered;
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

	src->self = source;
	obs_enter_graphics();
	src->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	if (!vss_effect) {
		char *f = obs_module_file("vectorscope.effect");
		vss_effect = gs_effect_create_from_file(f, NULL);
		if (!vss_effect)
			blog(LOG_ERROR, "Cannot load '%s'", f);
		bfree(f);
	}
	obs_leave_graphics();

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

	pthread_mutex_init(&src->target_update_mutex, NULL);

	vss_update(src, settings);

	return src;
}

static void vss_destroy(void *data)
{
	struct vss_source *src = data;

	obs_enter_graphics();
	gs_stagesurface_destroy(src->stagesurface);
	gs_texrender_destroy(src->texrender);

	gs_texture_destroy(src->tex_vs);
	bfree(src->tex_buf);
	gs_image_file_free(&src->graticule_img);
	gs_vertexbuffer_destroy(src->graticule_vbuf);
	obs_leave_graphics();

	bfree(src->target_name);
	pthread_mutex_destroy(&src->target_update_mutex);
	obs_weak_source_release(src->weak_target);

	bfree(src);
}

static void vss_update(void *data, obs_data_t *settings)
{
	struct vss_source *src = data;
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

	src->target_scale = obs_data_get_int(settings, "target_scale");
	if (src->target_scale<1)
		src->target_scale = 1;

	src->intensity = obs_data_get_int(settings, "intensity");
	if (src->intensity<1)
		src->intensity = 1;

	src->graticule = obs_data_get_int(settings, "graticule");

	int colorspace = obs_data_get_int(settings, "colorspace");
	if (colorspace!=src->colorspace) {
		src->colorspace = colorspace;
		src->colorspace_updated = 1;
	}
	src->bypass_vectorscope = obs_data_get_bool(settings, "bypass_vectorscope");
}

static void vss_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "graticule", 1);
}

static bool add_sources(void *data, obs_source_t *source)
{
	obs_property_t *prop = data;
	uint32_t caps = obs_source_get_output_flags(source);

	if (~caps & OBS_SOURCE_VIDEO)
		return true;

	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(prop, name, name);
	return true;
}

static obs_properties_t *vss_get_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	obs_properties_t *props;
	obs_property_t *prop;
	props = obs_properties_create();

	prop = obs_properties_add_list(props, "target_name", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_enum_scenes(add_sources, prop);
	obs_enum_sources(add_sources, prop);

	obs_properties_add_int(props, "target_scale", obs_module_text("Scale"), 1, 128, 1);
	obs_properties_add_int(props, "intensity", obs_module_text("Intensity"), 1, 255, 1);
	prop = obs_properties_add_list(props, "graticule", obs_module_text("Graticule"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, "None", 0);
	obs_property_list_add_int(prop, "Green", 1);

	prop = obs_properties_add_list(props, "colorspace", obs_module_text("Color space"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, "Auto", 0);
	obs_property_list_add_int(prop, "601", 1);
	obs_property_list_add_int(prop, "709", 2);

	obs_properties_add_bool(props, "bypass_vectorscope", obs_module_text("Bypass"));

	return props;
}

static uint32_t vss_get_width(void *data)
{
	struct vss_source *src = data;
	return src->bypass_vectorscope ? src->known_width : VS_SIZE;
}

static uint32_t vss_get_height(void *data)
{
	struct vss_source *src = data;
	return src->bypass_vectorscope ? src->known_height : VS_SIZE;
}

static void vss_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	struct vss_source *src = data;
	obs_source_t *target = obs_weak_source_get_source(src->weak_target);
	if (target) {
		enum_callback(src->self, target, param);
		obs_source_release(target);
	}
}

static inline void vss_draw_vectorscope(struct vss_source *src, uint8_t *video_data, uint32_t video_line)
{
	if (!src->tex_buf)
		src->tex_buf = bzalloc(VS_SIZE*VS_SIZE);
	uint8_t *dbuf = src->tex_buf;

	for (int i=0; i<VS_SIZE*VS_SIZE; i++)
		dbuf[i] = 0;

	const uint32_t height = src->known_height;
	const uint32_t width = src->known_width;
	for (uint32_t y=0; y<height; y++) {
		uint8_t *v = video_data + video_line * y;
		for (uint32_t x=0; x<width; x++) {
			const uint8_t b = *v++;
			const uint8_t g = *v++;
			const uint8_t r = *v++;
			const uint8_t a = *v++;
			if (!a) continue;
			int u;
			int v;
			switch (src->colorspace_calc) {
				case 1:// BT.601
					// y = (+306*r +601*g +117*b)/1024 +0;
					u = (-150*r -296*g +448*b)/1024 +128;
					v = (+448*r -374*g -72*b)/1024 +128;
					break;
				case 2: // BT.709
					// y = (+218*r +732*g +74*b)/1024 +16;
					u = (-102*r -346*g +450*b)/1024 +128;
					v = (+450*r -408*g -40*b)/1024 +128;
					break;
					u = -1;
					v = -1;
			}
			if (u<0 || 255<u || v<0 || 255<v)
				continue;
			uint8_t *c = dbuf + (u + VS_SIZE*(255-v));
			if (*c<255) ++*c;
		}
	}

	gs_texture_destroy(src->tex_vs);
	src->tex_vs = gs_texture_create(VS_SIZE, VS_SIZE, GS_R8, 1, (const uint8_t**)&src->tex_buf, 0);
}

static void vss_render_target(struct vss_source *src)
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

		gs_stage_texture(src->stagesurface, gs_texrender_get_texture(src->texrender));
		uint8_t *video_data = NULL;
		uint32_t video_linesize;
		if (gs_stagesurface_map(src->stagesurface, &video_data, &video_linesize)) {
			if (src->bypass_vectorscope) {
				gs_texture_destroy(src->tex_vs);
				src->tex_vs = gs_texture_create(width, height, GS_BGRA, 1, (const uint8_t**)&video_data, 0);
			}
			else
				vss_draw_vectorscope(src, video_data, video_linesize);
		}
		gs_stagesurface_unmap(src->stagesurface);

	}

end:
	obs_source_release(target);
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
	const int ppi = src->colorspace_calc-1;
	for (int i=0; i<12; i++) {
		const float x = pp[ppi][i][0];
		const float y = 256.f-pp[ppi][i][1];
		const float dx = 8.0f; // (x-y) * 8.0f / hypotf(y-128, x-128);
		const float dy = 8.0f; // (x+y-256) * 8.0f / hypotf(y-128, x-128);
		// set_v3_rect(vdata->points + i*6, x-8, y-8, 16, 16);
		vec3_set(vdata->points + i*6 + 0, x-dx, y-dy, 0.0f);
		vec3_set(vdata->points + i*6 + 1, x+dx, y-dy, 0.0f);
		vec3_set(vdata->points + i*6 + 2, x-dx, y+dy, 0.0f);
		vec3_set(vdata->points + i*6 + 3, x-dx, y+dy, 0.0f);
		vec3_set(vdata->points + i*6 + 4, x+dx, y-dy, 0.0f);
		vec3_set(vdata->points + i*6 + 5, x+dx, y+dy, 0.0f);
		set_v2_uv(tvarray + i*6, 6./7., 0, 7./7., 1);
	}
	for (int i=0; i<6; i++) {
		float x = pp[ppi][i][0];
		float y = 256.f-pp[ppi][i][1];
		if      (x <  72) y += 20;
		else if (x > 184) y -= 20;
		else if (y > 128) x += 20;
		else              x -= 20;
		set_v3_rect(vdata->points + (i+12)*6, x-8, y-8, 16, 16);
		set_v2_uv(tvarray + (i+12)*6, i/7., 0, (i+1)/7., 1);
	}

	obs_leave_graphics();
}

static void vss_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct vss_source *src = data;

	if (src->colorspace_updated || src->colorspace_calc<1) {
		src->colorspace_calc = src->colorspace;
		src->colorspace_updated = 0;
		if (src->colorspace_calc<1 || 2<src->colorspace_calc) {
			struct obs_video_info ovi;
			if (obs_get_video_info(&ovi)) {
				switch (ovi.colorspace) {
					case VIDEO_CS_601:
						src->colorspace_calc = 1;
						break;
					case VIDEO_CS_709:
					default:
						src->colorspace_calc = 2;
						break;
				}
			}
		}
		gs_vertexbuffer_destroy(src->graticule_vbuf);
		src->graticule_vbuf = NULL;
	}

	vss_render_target(src);

	if (src->bypass_vectorscope && src->tex_vs) {
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), src->tex_vs);
		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite(src->tex_vs, 0, src->known_width, src->known_height);
		}
		return;
	}

	if (src->tex_vs) {
		gs_effect_t *effect = vss_effect ? vss_effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), src->tex_vs);
		gs_effect_set_float(gs_effect_get_param_by_name(effect, "intensity"), src->intensity);
		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite(src->tex_vs, 0, VS_SIZE, VS_SIZE);
		}
	}

	if (src->graticule_img.loaded && src->graticule) {
		create_graticule_vbuf(src);
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		draw_uv_vbuffer(src->graticule_vbuf, src->graticule_img.texture, effect, N_GRATICULES*6);
	}
}

static void vss_tick(void *data, float unused)
{
	UNUSED_PARAMETER(unused);
	struct vss_source *src = data;

	pthread_mutex_lock(&src->target_update_mutex);
	if (src->target_name && *src->target_name && !src->weak_target && src->target_check_time) {
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

struct obs_source_info colormonitor_vectorscope = {
	.id = "vectorscope_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = vss_get_name,
	.create = vss_create,
	.destroy = vss_destroy,
	.update = vss_update,
	.get_defaults = vss_get_defaults,
	.get_properties = vss_get_properties,
	.get_width = vss_get_width,
	.get_height = vss_get_height,
	.enum_active_sources = vss_enum_sources,
	.video_render = vss_render,
	.video_tick = vss_tick,
};
