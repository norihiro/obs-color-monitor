#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct roi_source
{
	struct cm_source cm;

	int x0, x1, y0, y1;
	volatile int x0in, x1in, y0in, y1in;
	int x_start, y_start;

	int n_rgb, n_uv, n_y;
	bool b_rgb, b_yuv;
};

static inline void roi_request_rgb(struct roi_source *src) { src->n_rgb = 2; }
static inline void roi_request_uv(struct roi_source *src) { src->n_uv = 2; }
static inline void roi_request_y(struct roi_source *src) { src->n_y = 2; }

struct roi_source *roi_from_source(obs_source_t *);
void roi_target_render(struct roi_source *src);
bool roi_stagesurfae_map(struct roi_source *, uint8_t **, uint32_t *, int ix);
void roi_stagesurfae_unmap(struct roi_source *);

static inline uint32_t roi_width(struct roi_source *src)
{
	if (0 <= src->x0 && src->x0 <= src->x1 && src->x1 <= src->cm.known_width)
		return src->x1 - src->x0;
	return src->cm.known_width;
}

static inline uint32_t roi_height(struct roi_source *src)
{
	if (0 <= src->y0 && src->y0 <= src->y1 && src->y1 <= src->cm.known_height)
		return src->y1 - src->y0;
	return src->cm.known_height;
}

#ifdef __cplusplus
}
#endif
