#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct roi_surface_info_s
{
	int x0, y0;
	int w, h;
	int surface_height;
	bool b_rgb, b_yuv;
};

struct roi_source
{
	struct cm_source cm;
	int n_interleave, i_interleave;
	bool interleave_rendered;

	int x0, x1, y0, y1;
	struct roi_surface_info_s roi_surface_pos_next;
	struct roi_surface_info_s roi_surface_pos;
	int x0sizing, x1sizing, y0sizing, y1sizing;
	int x0in, x1in, y0in, y1in;
	uint32_t flags_interact;
	uint32_t flags_interact_gs;
	int x_start, y_start;
	int x_mouse, y_mouse;

	int n_rgb, n_uv, n_y;
	bool b_rgb, b_yuv;
};

static inline void roi_request_rgb(struct roi_source *src) { src->n_rgb = 4; }
static inline void roi_request_uv(struct roi_source *src) { src->n_uv = 4; }
static inline void roi_request_y(struct roi_source *src) { src->n_y = 4; }

struct roi_source *roi_from_source(obs_source_t *);
bool roi_target_render(struct roi_source *src);
bool roi_stagesurface_map(struct roi_source *, uint8_t **, uint32_t *, int ix);
void roi_stagesurface_unmap(struct roi_source *);

static inline uint32_t roi_width(struct roi_source *src)
{
	return src->roi_surface_pos.w;
}

static inline uint32_t roi_height(struct roi_source *src)
{
	return src->roi_surface_pos.h;
}

#ifdef __cplusplus
}
#endif
