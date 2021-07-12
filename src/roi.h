#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct roi_source
{
	struct cm_source cm;

	int x0, x1, y0, y1;
	int x_start, y_start;

	int n_rgb, n_uv, n_y;
};

#ifdef __cplusplus
}
#endif
