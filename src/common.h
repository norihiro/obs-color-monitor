#pragma once

#include <util/threading.h>

#ifdef __cplusplus
extern "C" {
#endif

void property_list_add_sources(obs_property_t *prop, obs_source_t *self);

static inline bool is_preview_name(const char *name)
{
	return *name && name[0]==0x10 && name[1]==0;
}

struct cm_source
{
	// graphics
	obs_source_t *self;
	gs_texrender_t *texrender;
	gs_texrender_t *texrender_yuv;
	gs_stagesurf_t* stagesurface;
	uint32_t known_width;
	uint32_t known_height;
	bool rendered;

	bool enumerating; // not thread safe but I have no other idea.

	// target
	pthread_mutex_t target_update_mutex;
	uint64_t target_check_time;
	obs_weak_source_t *weak_target;
	obs_source_t *target;
	struct roi_source *roi;
	char *target_name;

	// properties
	int target_scale;
	int colorspace; // get from ovi if auto
	uint32_t flags;
	bool bypass;
};

#define CM_FLAG_CONVERT_RGB 1
#define CM_FLAG_CONVERT_UV  2
#define CM_FLAG_CONVERT_Y   4
#define CM_FLAG_ROI         8

void cm_create(struct cm_source *src, obs_data_t *settings, obs_source_t *source);
void cm_destroy(struct cm_source *src);
void cm_update(struct cm_source *src, obs_data_t *settings);
void cm_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param);
void cm_get_properties(struct cm_source *src, obs_properties_t *props);
bool cm_render_target(struct cm_source *src);
bool cm_stagesurface_map(struct cm_source *src, uint8_t **video_data, uint32_t *video_linesize);
void cm_stagesurface_unmap(struct cm_source *src);
void cm_render_bypass(struct cm_source *src);
void cm_tick(void *data, float unused);

int calc_colorspace(int);

#ifdef __cplusplus
}
#endif
