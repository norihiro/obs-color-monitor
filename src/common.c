#include <obs-module.h>
#include <util/platform.h>
#include <obs-frontend-api.h>
#include "plugin-macros.generated.h"
#include "obs-convenience.h"
#include "common.h"
#include "roi.h"

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_render_name_b = "cm_render_bypass";
static const char *prof_render_target_name = "render_target";
static const char *prof_convert_yuv_name = "convert_yuv";
static const char *prof_stage_surface_name = "stage_surface";
static const char *prof_stagesurface_map_name = "stage_surface_map";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

#define SOURCE_CHECK_NS 3000000000

gs_effect_t *cm_rgb2yuv_effect = NULL;

void cm_create(struct cm_source *src, obs_data_t *settings, obs_source_t *source)
{
	src->self = source;

	// TODO: consider delayed allocation
	obs_enter_graphics();
	src->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	src->texrender_yuv = gs_texrender_create(GS_BGRA, GS_ZS_NONE);

	if (!cm_rgb2yuv_effect) {
		char *f = obs_module_file("vectorscope.effect");
		cm_rgb2yuv_effect = gs_effect_create_from_file(f, NULL);
		if (!cm_rgb2yuv_effect)
			blog(LOG_ERROR, "Cannot load '%s'", f);
		bfree(f);
	}
	obs_leave_graphics();

	pthread_mutex_init(&src->target_update_mutex, NULL);
}

void cm_destroy(struct cm_source *src)
{
	if (src->target) {
		obs_source_release(src->target);
		src->roi = NULL;
		src->target = NULL;
	}

	obs_enter_graphics();
	gs_stagesurface_destroy(src->stagesurface);
	gs_texrender_destroy(src->texrender_yuv);
	gs_texrender_destroy(src->texrender);
	obs_leave_graphics();

	pthread_mutex_destroy(&src->target_update_mutex);
	obs_weak_source_release(src->weak_target);

	bfree(src->target_name);
}

void cm_update(struct cm_source *src, obs_data_t *settings)
{
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

	src->bypass = obs_data_get_bool(settings, "bypass");
}

void cm_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	struct cm_source *src = data;
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

void cm_get_properties(struct cm_source *src, obs_properties_t *props)
{
	obs_property_t *prop;

	prop = obs_properties_add_list(props, "target_name", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	property_list_add_sources(prop, src ? src->self : NULL);
	obs_properties_add_int(props, "target_scale", obs_module_text("Scale"), 1, 128, 1);

	if (!(src->flags & CM_FLAG_ROI))
		obs_properties_add_bool(props, "bypass", obs_module_text("Bypass"));
}

bool cm_render_target(struct cm_source *src)
{
	if (src->rendered)
		return false;
	src->rendered = 1;

	obs_source_t *target = src->weak_target ? obs_weak_source_get_source(src->weak_target) : NULL;
	if (!target && *src->target_name)
		return false;

	if (target && !src->bypass) {
		struct roi_source *roi = roi_from_source(target);
		if (roi) {
			if (src->flags & CM_FLAG_CONVERT_UV)
				roi_request_uv(roi);
			if (src->flags & CM_FLAG_CONVERT_Y)
				roi_request_y(roi);
			if (!(src->flags & (CM_FLAG_CONVERT_UV | CM_FLAG_CONVERT_Y)))
				roi_request_rgb(roi);
			roi_target_render(roi);
			src->known_width = roi_width(roi);
			src->known_height = roi_height(roi);
			src->target = target;
			src->roi = roi;
			return true;
		}
	}

	int target_width, target_height;
	if (target) {
		target_width = obs_source_get_width(target);
		target_height = obs_source_get_height(target);
	}
	else {
		struct obs_video_info ovi;
		obs_get_video_info(&ovi);
		target_width = ovi.base_width;
		target_height = ovi.base_height;
	}
	int width = target_width / src->target_scale;
	int height = target_height / src->target_scale;
	if (width<=0 || height<=0) {
		if (target)
			obs_source_release(target);
		return false;
	}

	PROFILE_START(prof_render_target_name);

	gs_texrender_reset(src->texrender);
	if (gs_texrender_begin(src->texrender, width, height)) {
		struct vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)target_width, 0.0f, (float)target_height, -100.0f, 100.0f);

		gs_blend_state_push();
		if (target) {
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
			obs_source_video_render(target);
		}
		else
			obs_render_main_texture();

		gs_texrender_end(src->texrender);

		if (width != src->known_width || height != src->known_height) {
			if (!(src->flags & CM_FLAG_ROI)) {
				gs_stagesurface_destroy(src->stagesurface);
				src->stagesurface = gs_stagesurface_create(width, height, GS_BGRA);
			}
			src->known_width = width;
			src->known_height = height;
		}

		PROFILE_END(prof_render_target_name);

		if (src->bypass || (src->flags & CM_FLAG_ROI)) {
			// do nothing
		}
		else if (src->flags & CM_FLAG_CONVERT_UV) {
			gs_texrender_reset(src->texrender_yuv);
			if (cm_rgb2yuv_effect && gs_texrender_begin(src->texrender_yuv, width, height)) {
				PROFILE_START(prof_convert_yuv_name);
				gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

				gs_effect_t *effect = cm_rgb2yuv_effect;
				gs_texture_t *tex = gs_texrender_get_texture(src->texrender);
				if (tex) {
					gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
					while (gs_effect_loop(effect, src->colorspace==1 ? "ConvertRGB_UV601" : "ConvertRGB_UV709")) {
						gs_draw_sprite(tex, 0, width, height);
					}
				}
				gs_texrender_end(src->texrender_yuv);
				PROFILE_END(prof_convert_yuv_name);

				PROFILE_START(prof_stage_surface_name);
				gs_stage_texture(src->stagesurface, gs_texrender_get_texture(src->texrender_yuv));
				PROFILE_END(prof_stage_surface_name);
			}
		}
		else /* stage RGB format */ {
			PROFILE_START(prof_stage_surface_name);
			gs_stage_texture(src->stagesurface, gs_texrender_get_texture(src->texrender));
			PROFILE_END(prof_stage_surface_name);
		}
		gs_blend_state_pop();
	}
	else
		PROFILE_END(prof_render_target_name);

	if (target)
		obs_source_release(target);
	return true;
}

bool cm_stagesurface_map(struct cm_source *src, uint8_t **video_data, uint32_t *video_linesize)
{
	if (src->roi) {
		int ix = (src->flags & (CM_FLAG_CONVERT_UV | CM_FLAG_CONVERT_Y)) ? 1 : 0;
		return roi_stagesurfae_map(src->roi, video_data, video_linesize, ix);
	}

	PROFILE_START(prof_stagesurface_map_name);
	bool ret = gs_stagesurface_map(src->stagesurface, video_data, video_linesize);
	PROFILE_END(prof_stagesurface_map_name);
	return ret;
}

void cm_stagesurface_unmap(struct cm_source *src)
{
	if (src->roi) {
		roi_stagesurfae_unmap(src->roi);
		return;
	}

	gs_stagesurface_unmap(src->stagesurface);
}

void cm_render_bypass(struct cm_source *src)
{
	PROFILE_START(prof_render_name_b);

	cm_render_target(src);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_texture_t *tex = gs_texrender_get_texture(src->texrender);
	if (!tex)
		goto end;
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
	while (gs_effect_loop(effect, "Draw")) {
		gs_draw_sprite(tex, 0, src->known_width, src->known_height);
	}

end:;
	PROFILE_END(prof_render_name_b);
}

void cm_tick(void *data, float unused)
{
	UNUSED_PARAMETER(unused);
	struct cm_source *src = data;

	if (src->target) {
		obs_source_release(src->target);
		src->roi = NULL;
		src->target = NULL;
	}

	pthread_mutex_lock(&src->target_update_mutex);
	if (src->target_name && !*src->target_name) {
		if (src->weak_target)
			obs_weak_source_release(src->weak_target);
		src->weak_target = NULL;
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
