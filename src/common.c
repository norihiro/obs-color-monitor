#include <obs-module.h>
#include <util/platform.h>
#include <obs-frontend-api.h>
#include "plugin-macros.generated.h"
#include "obs-convenience.h"
#include "common.h"
#include "util.h"
#include "roi.h"

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_pipeline_thread = "cm_pipeline_thread_loop";
static const char *prof_render_target_name = "render_target";
static const char *prof_convert_yuv_name = "convert_yuv";
static const char *prof_stage_surface_name = "stage_surface";
static const char *prof_stagesurface_map_name = "stage_surface_map";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

void cm_create(struct cm_source *src, obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	src->self = source;

	// To avoid continuously load the file if the file does not exist, load the file here.
	obs_enter_graphics();
	src->effect = create_effect_from_module_file("common.effect");
	obs_leave_graphics();

	src->i_write_queue = 0;
	src->i_staging_queue = 0;
	src->i_read_queue = CM_SURFACE_QUEUE_SIZE - 1;

	pthread_mutex_init(&src->target_update_mutex, NULL);
	pthread_mutex_init(&src->pipeline_mutex, NULL);
	pthread_cond_init(&src->pipeline_cond, NULL);
}

static void release_roi_src(struct cm_source *src);
static void stop_pipeline_thread(struct cm_source *src);

void cm_destroy(struct cm_source *src)
{
	if (src->roi_src) {
		release_roi_src(src);
	}

	stop_pipeline_thread(src);

	obs_enter_graphics();
	for (int i = 0; i < CM_SURFACE_QUEUE_SIZE; i++) {
		gs_stagesurface_destroy(src->queue[i].stagesurface);
		gs_texrender_destroy(src->queue[i].texrender);
	}
	if (src->texrender)
		gs_texrender_destroy(src->texrender);
	obs_leave_graphics();

	pthread_mutex_destroy(&src->pipeline_mutex);
	pthread_cond_destroy(&src->pipeline_cond);

	pthread_mutex_destroy(&src->target_update_mutex);
	obs_weak_source_release(src->weak_target);

	bfree(src->target_name);
}

void cm_update(struct cm_source *src, obs_data_t *settings)
{
	obs_weak_source_t *weak_source_old = NULL;

	const char *target_name = obs_data_get_string(settings, "target_name");
	if (target_name && (!src->target_name || strcmp(target_name, src->target_name))) {
		pthread_mutex_lock(&src->target_update_mutex);
		bfree(src->target_name);
		src->target_name = bstrdup(target_name);
		pthread_mutex_unlock(&src->target_update_mutex);
	}

	if (weak_source_old) {
		obs_weak_source_release(weak_source_old);
		weak_source_old = NULL;
	}

	src->target_scale = (int)obs_data_get_int(settings, "target_scale");
	if (src->target_scale < 1)
		src->target_scale = 1;

	src->bypass = obs_data_get_bool(settings, "bypass");

	int colorspace = (int)obs_data_get_int(settings, "colorspace");
	src->colorspace = calc_colorspace(colorspace);
}

void cm_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	struct cm_source *src = data;
	if (src->enumerating)
		return;
	src->enumerating = 1;
	pthread_mutex_lock(&src->target_update_mutex);
	obs_source_t *target = obs_weak_source_get_source(src->weak_target);
	pthread_mutex_unlock(&src->target_update_mutex);
	if (target) {
		enum_callback(src->self, target, param);
		obs_source_release(target);
	}
	src->enumerating = 0;
}

void cm_get_properties(struct cm_source *src, obs_properties_t *props)
{
	if (!src)
		return;

	obs_property_t *prop;

	prop = obs_properties_add_list(props, "target_name", obs_module_text("Source"), OBS_COMBO_TYPE_LIST,
				       OBS_COMBO_FORMAT_STRING);
	property_list_add_sources(prop, src ? src->self : NULL);
	obs_properties_add_int(props, "target_scale", obs_module_text("Scale"), 1, 128, 1);

	if (!(src->flags & CM_FLAG_ROI))
		obs_properties_add_bool(props, "bypass", obs_module_text("Bypass"));
}

static void prepare_stagesurface(struct cm_surface_queue_item *item, uint32_t width, uint32_t height, uint32_t sheight)
{
	if (width != item->width || sheight != item->sheight || !item->stagesurface) {
		gs_stagesurface_destroy(item->stagesurface);
		item->stagesurface = gs_stagesurface_create(width, sheight, GS_BGRA);
		item->width = width;
		item->sheight = sheight;
	}
	item->height = height;
}

static bool render_target_to_texrender(obs_source_t *target, uint32_t target_width, uint32_t target_height,
				       gs_texrender_t *texrender, uint32_t width, uint32_t height)
{
	gs_texrender_reset(texrender);
	if (!gs_texrender_begin(texrender, width, height))
		return false;

	struct vec4 background;
	vec4_zero(&background);

	gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);

	gs_projection_push();
	gs_ortho(0.0f, (float)target_width, 0.0f, (float)target_height, -100.0f, 100.0f);

	gs_blend_state_push();
	if (target) {
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(target);
	} else {
		obs_render_main_texture();
	}
	gs_blend_state_pop();
	gs_projection_pop();

	gs_texrender_end(texrender);
	return true;
}

static bool render_rgb_yuv(struct cm_source *src, struct cm_surface_queue_item *item, uint32_t x, uint32_t y)
{
	if (!item->texrender)
		item->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);

	gs_texrender_reset(item->texrender);
	if (src->effect && gs_texrender_begin(item->texrender, item->width, item->sheight)) {
		PROFILE_START(prof_convert_yuv_name);

		struct vec4 background;
		vec4_zero(&background);
		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);

		gs_projection_push();
		gs_ortho(0.0f, (float)item->width, 0.0f, (float)item->sheight, -100.0f, 100.0f);

		gs_texture_t *tex = gs_texrender_get_texture(src->texrender);
		if (tex) {

			uint32_t offset = 0;

			if (item->flags & (CM_FLAG_CONVERT_RGB | CM_FLAG_RAW_TEXTURE)) {
				gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);

				gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
				while (gs_effect_loop(effect, "Draw")) {
					gs_draw_sprite_subregion(tex, 0, x, y, item->width, item->height);
				}

				offset += item->height;
			}

			if (item->flags & CM_FLAG_CONVERT_YUV) {
				if (offset) {
					gs_matrix_translate3f(0.0f, (float)offset, 0.0f);
				}

				const char *conversion = src->colorspace == 1 ? "ConvertRGB_YUV601"
									      : "ConvertRGB_YUV709";
				gs_effect_set_texture(gs_effect_get_param_by_name(src->effect, "image"), tex);
				while (gs_effect_loop(src->effect, conversion)) {
					gs_draw_sprite_subregion(tex, 0, x, y, item->width, item->height);
				}
			}
		}
		gs_texrender_end(item->texrender);
		gs_projection_pop();
		PROFILE_END(prof_convert_yuv_name);
	}

	return true;
}

void cm_render_target(struct cm_source *src)
{
	if (src->rendered)
		return;
	src->rendered = 1;

	if (cm_is_roi(src)) {
		// Call roi_target_render just in case ROI is not rendered.
		roi_target_render(src->roi);
		return;
	}

	obs_source_t *target = src->weak_target ? obs_weak_source_get_source(src->weak_target) : NULL;
	if (!target && *src->target_name)
		return;

	uint32_t target_width, target_height;
	if (target) {
		target_width = obs_source_get_width(target);
		target_height = obs_source_get_height(target);
	} else {
		struct obs_video_info ovi;
		obs_get_video_info(&ovi);
		target_width = ovi.base_width;
		target_height = ovi.base_height;
	}
	uint32_t scaled_width = target_width / src->target_scale;
	uint32_t scaled_height = target_height / src->target_scale;
	if (scaled_width <= 0 || scaled_height <= 0) {
		obs_source_release(target);
		return;
	}

	bool has_rgb = !src->bypass && (src->flags & CM_FLAG_CONVERT_RGB);
	bool has_yuv = !src->bypass && (src->flags & CM_FLAG_CONVERT_YUV);
	bool has_raw = src->bypass || (src->flags & CM_FLAG_RAW_TEXTURE);

	if ((has_rgb || has_yuv) && src->i_write_queue == src->i_read_queue) {
		pthread_mutex_lock(&src->pipeline_mutex);
		src->i_staging_queue = -1;
		pthread_cond_signal(&src->pipeline_cond);
		pthread_mutex_unlock(&src->pipeline_mutex);

		obs_source_release(target);
		return;
	}

	PROFILE_START(prof_render_target_name);

	uint32_t x, y, cx, cy;
	if ((src->flags & CM_FLAG_ROI) && 0 <= src->x0 && src->x0 < src->x1 && 0 <= src->y0 && src->y0 < src->y1) {
		x = src->x0;
		y = src->y0;
		cx = src->x1 - x;
		cy = src->y1 - y;
	} else {
		x = y = 0;
		cx = scaled_width;
		cy = scaled_height;
	}

	uint32_t sheight = 0;
	struct cm_surface_queue_item *item = &src->queue[src->i_write_queue];
	if (has_rgb || has_raw) {
		sheight += cy;
	}
	if (has_yuv) {
		sheight += cy;
	}
	item->cb = src->callback;
	item->cb_data = src->callback_data;
	item->flags = src->bypass ? CM_FLAG_RAW_TEXTURE
				  : src->flags & (CM_FLAG_CONVERT_RGB | CM_FLAG_CONVERT_YUV | CM_FLAG_RAW_TEXTURE);
	item->colorspace = src->colorspace;

	prepare_stagesurface(item, cx, cy, sheight);

	if (!src->texrender)
		src->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);

	if (!render_target_to_texrender(target, target_width, target_height, src->texrender, scaled_width,
					scaled_height)) {
		obs_source_release(target);
		return;
	}
	src->texrender_width = scaled_width;
	src->texrender_height = scaled_height;

	if (has_rgb || has_raw || has_yuv)
		render_rgb_yuv(src, item, x, y);

	PROFILE_END(prof_render_target_name);

	if (has_rgb || has_yuv) {
		PROFILE_START(prof_stage_surface_name);
		gs_stage_texture(item->stagesurface, gs_texrender_get_texture(item->texrender));
		PROFILE_END(prof_stage_surface_name);
	}

	pthread_mutex_lock(&src->pipeline_mutex);
	src->i_staging_queue = src->i_write_queue;
	src->i_write_queue = (src->i_write_queue + 1) % CM_SURFACE_QUEUE_SIZE;
	if (has_rgb || has_yuv)
		pthread_cond_signal(&src->pipeline_cond);
	else
		src->i_read_queue = (src->i_write_queue + CM_SURFACE_QUEUE_SIZE - 1) % CM_SURFACE_QUEUE_SIZE;
	pthread_mutex_unlock(&src->pipeline_mutex);

	if (target)
		obs_source_release(target);
}

static void cm_pipeline_thread_loop(struct cm_surface_queue_item *item)
{
	uint8_t *video_data;
	uint32_t video_linesize;

	if (!(item->flags & (CM_FLAG_CONVERT_RGB | CM_FLAG_CONVERT_YUV)))
		return;

	obs_enter_graphics();
	PROFILE_START(prof_stagesurface_map_name);
	bool ret = gs_stagesurface_map(item->stagesurface, &video_data, &video_linesize);
	PROFILE_END(prof_stagesurface_map_name);
	obs_leave_graphics();

	if (!ret)
		return;

	struct cm_surface_data surface_data = {
		.linesize = video_linesize,
		.width = item->width,
		.height = item->height,
		.colorspace = item->colorspace,
	};
	if (item->flags & CM_FLAG_CONVERT_RGB) {
		surface_data.rgb_data = video_data;
		video_data += video_linesize * item->height;
	}
	if (item->flags & CM_FLAG_CONVERT_YUV) {
		surface_data.yuv_data = video_data;
	}

	if (item->cb) {
		item->cb(item->cb_data, &surface_data);
	}

	obs_enter_graphics();
	gs_stagesurface_unmap(item->stagesurface);
	obs_leave_graphics();
}

static void *cm_pipeline_thread(void *data)
{
	blog(LOG_DEBUG, "entering cm_pipeline_thread data=%p", data);
	struct cm_source *src = data;

	pthread_mutex_lock(&src->pipeline_mutex);
	while (!src->request_exit) {
		int next = (src->i_read_queue + 1) % CM_SURFACE_QUEUE_SIZE;
		if (src->i_write_queue == next || src->i_staging_queue == next) {
			pthread_cond_wait(&src->pipeline_cond, &src->pipeline_mutex);
			continue;
		}

		src->i_read_queue = next;
		pthread_mutex_unlock(&src->pipeline_mutex);

		PROFILE_START(prof_pipeline_thread);
		cm_pipeline_thread_loop(&src->queue[src->i_read_queue]);
		PROFILE_END(prof_pipeline_thread);
		pthread_mutex_lock(&src->pipeline_mutex);
	}
	pthread_mutex_unlock(&src->pipeline_mutex);

	blog(LOG_DEBUG, "leaving cm_pipeline_thread data=%p", data);

	return NULL;
}

static const struct cm_surface_queue_item *get_last_written_surface(const struct cm_source *src)
{
	if (cm_is_roi(src))
		return get_last_written_surface(&src->roi->cm);

	return &src->queue[src->i_bypass_queue];
}

void cm_bypass_render(struct cm_source *src)
{
	cm_render_target(src);

	const struct cm_surface_queue_item *item = get_last_written_surface(src);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);

	gs_texture_t *tex = gs_texrender_get_texture(item->texrender);
	if (!tex)
		return;
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
	while (gs_effect_loop(effect, "Draw")) {
		gs_draw_sprite_subregion(tex, 0, 0, 0, item->width, item->height);
	}
}

static void stop_pipeline_thread(struct cm_source *src)
{
	if (!src->pipeline_thread_running)
		return;

	pthread_mutex_lock(&src->pipeline_mutex);
	src->request_exit = true;
	pthread_cond_signal(&src->pipeline_cond);
	pthread_mutex_unlock(&src->pipeline_mutex);

	pthread_join(src->pipeline_thread, NULL);
	src->pipeline_thread_running = false;
}

static void start_pipeline_thread(struct cm_source *src)
{
	if (src->pipeline_thread_running)
		return;

	src->request_exit = false;

	if (pthread_create(&src->pipeline_thread, NULL, cm_pipeline_thread, src) == 0) {
		src->pipeline_thread_running = true;
	}
}

static bool update_target_unlocked_program(struct cm_source *src)
{
	if (src->weak_target) {
		obs_weak_source_release(src->weak_target);
		src->weak_target = NULL;
		return true;
	}
	return false;
}

static bool update_target_unlocked_mainview(struct cm_source *src)
{
	obs_source_t *target = obs_get_output_source(0);
	obs_weak_source_t *weak_target = obs_source_get_weak_source(target);
	obs_source_release(target);

	if (weak_target != src->weak_target) {
		obs_weak_source_release(src->weak_target);
		src->weak_target = weak_target;
		return true;
	} else {
		obs_weak_source_release(weak_target);
		return false;
	}
}

static bool update_target_unlocked_preview(struct cm_source *src)
{
	obs_source_t *target = obs_frontend_get_current_preview_scene();
	obs_weak_source_t *weak_target = obs_source_get_weak_source(target);
	obs_source_release(target);

	if (weak_target != src->weak_target) {
		obs_weak_source_release(src->weak_target);
		src->weak_target = weak_target;
		return true;
	} else {
		obs_weak_source_release(weak_target);
		return false;
	}
}

static bool update_target_unlocked_by_name(struct cm_source *src)
{
	if (src->weak_target) {
		obs_source_t *current_target = obs_weak_source_get_source(src->weak_target);

		if (obs_source_removed(current_target)) {
			obs_source_release(current_target);
			current_target = NULL;
		}

		if (current_target) {
			const char *current_name = obs_source_get_name(current_target);
			if (current_name && strcmp(current_name, src->target_name) == 0) {
				obs_source_release(current_target);
				return false;
			}

			obs_source_release(current_target);
		}

		obs_weak_source_release(src->weak_target);
		src->weak_target = NULL;
	}

	obs_source_t *target = obs_get_source_by_name(src->target_name);
	src->weak_target = obs_source_get_weak_source(target);
	obs_source_release(target);
	return true;
}

static bool update_target_unlocked(struct cm_source *src)
{
	if (!src->target_name)
		return false;

	if (is_program_name(src->target_name))
		return update_target_unlocked_program(src);

	if (is_mainview_name(src->target_name))
		return update_target_unlocked_mainview(src);

	if (is_preview_name(src->target_name))
		return update_target_unlocked_preview(src);

	return update_target_unlocked_by_name(src);
}

static void release_roi_src(struct cm_source *src)
{
	if (src->roi)
		roi_unregister_source(src->roi, src);

	src->roi = NULL;

	if (src->roi_src) {
		obs_source_release(src->roi_src);
		src->roi_src = NULL;
	}
}

static void update_roi_src(struct cm_source *src)
{
	if (!src->weak_target)
		return;

	obs_source_t *target = obs_weak_source_get_source(src->weak_target);
	src->roi = roi_from_source(target);
	if (!src->roi) {
		obs_source_release(target);
		return;
	}

	src->roi_src = target;

	roi_register_source(src->roi, src);
}

void cm_tick(void *data, float unused)
{
	UNUSED_PARAMETER(unused);
	struct cm_source *src = data;

	pthread_mutex_lock(&src->target_update_mutex);
	if (update_target_unlocked(src)) {
		release_roi_src(src);
		update_roi_src(src);
	}
	pthread_mutex_unlock(&src->target_update_mutex);

	if (src->roi && src->roi_src)
		stop_pipeline_thread(src);
	else if (!src->roi && (is_program_name(src->target_name) || src->weak_target))
		start_pipeline_thread(src);

	src->rendered = 0;

	src->i_bypass_queue = (src->i_write_queue + CM_SURFACE_QUEUE_SIZE - 1) % CM_SURFACE_QUEUE_SIZE;
}

uint32_t cm_bypass_get_width(struct cm_source *src)
{
	const struct cm_surface_queue_item *item = get_last_written_surface(src);
	return item->width;
}

uint32_t cm_bypass_get_height(struct cm_source *src)
{
	const struct cm_surface_queue_item *item = get_last_written_surface(src);
	return item->height;
}

gs_texture_t *cm_bypass_get_texture(struct cm_source *src)
{
	const struct cm_surface_queue_item *item = get_last_written_surface(src);
	return gs_texrender_get_texture(item->texrender);
}

void cm_request(struct cm_source *src, cm_surface_cb_t callback, void *data)
{
	// Should be called from the graphics thread or before start to operate.
	src->callback = callback;
	src->callback_data = data;
}
