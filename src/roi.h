#pragma once

#include <util/darray.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct roi_source
{
	struct cm_source cm;
	int n_interleave, i_interleave;
	bool interleave_rendered;

	int x0sizing, x1sizing, y0sizing, y1sizing;
	int x0in, x1in, y0in, y1in;
	uint32_t flags_interact;
	uint32_t flags_interact_gs;
	int x_start, y_start;
	int x_mouse, y_mouse;

	pthread_mutex_t sources_mutex;
	DARRAY(struct cm_source *) sources;
};


struct roi_source *roi_from_source(obs_source_t *);
bool roi_target_render(struct roi_source *src);

void roi_register_source(struct roi_source *, struct cm_source *);
void roi_unregister_source(struct roi_source *, struct cm_source *);

#ifdef __cplusplus
}
#endif
