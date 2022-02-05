/*
OBS Color Monitor
Copyright (C) 2021 Norihiro Kamae

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>

#include "plugin-macros.generated.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

extern struct obs_source_info colormonitor_vectorscope;
extern struct obs_source_info colormonitor_waveform;
extern struct obs_source_info colormonitor_histogram;
extern struct obs_source_info colormonitor_zebra;
extern struct obs_source_info colormonitor_zebra_filter;
extern struct obs_source_info colormonitor_falsecolor;
extern struct obs_source_info colormonitor_falsecolor_filter;
extern struct obs_source_info colormonitor_roi;
extern gs_effect_t *cm_rgb2yuv_effect;
extern gs_effect_t *wvs_effect;
extern gs_effect_t *his_effect;
void scope_docks_init();
void scope_docks_release();

bool obs_module_load(void)
{
	obs_register_source(&colormonitor_vectorscope);
	obs_register_source(&colormonitor_waveform);
	obs_register_source(&colormonitor_histogram);
	obs_register_source(&colormonitor_zebra);
	obs_register_source(&colormonitor_zebra_filter);
	obs_register_source(&colormonitor_falsecolor);
	obs_register_source(&colormonitor_falsecolor_filter);
	obs_register_source(&colormonitor_roi);
	scope_docks_init();
	blog(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload()
{
	if (cm_rgb2yuv_effect) {
		gs_effect_destroy(cm_rgb2yuv_effect);
		cm_rgb2yuv_effect = NULL;
	}
	if (wvs_effect) {
		gs_effect_destroy(wvs_effect);
		wvs_effect = NULL;
	}
	if (his_effect) {
		gs_effect_destroy(his_effect);
		his_effect = NULL;
	}
	scope_docks_release();
	blog(LOG_INFO, "plugin unloaded");
}
