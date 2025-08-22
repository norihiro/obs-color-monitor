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
#include <stdlib.h>
#include <util/config-file.h>
#include <obs-frontend-api.h>

#include "plugin-macros.generated.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define CONFIG_SECTION_NAME "ColorMonitor"

extern const struct obs_source_info colormonitor_vectorscope_v1;
extern const struct obs_source_info colormonitor_vectorscope;
extern const struct obs_source_info colormonitor_waveform;
extern const struct obs_source_info colormonitor_histogram;
extern const struct obs_source_info colormonitor_zebra;
extern const struct obs_source_info colormonitor_zebra_filter;
extern const struct obs_source_info colormonitor_falsecolor;
extern const struct obs_source_info colormonitor_falsecolor_filter;
extern const struct obs_source_info colormonitor_focuspeaking;
extern const struct obs_source_info colormonitor_focuspeaking_filter;
extern const struct obs_source_info colormonitor_roi;
void scope_docks_init();

static bool register_source_with_flags(const struct obs_source_info *const_info, uint32_t flags)
{
	struct obs_source_info info = *const_info;
	info.output_flags |= flags;
	obs_register_source(&info);

	if (!obs_get_latest_input_type_id(info.id)) {
		blog(LOG_ERROR, "failed to load source '%s'", info.id);
		return false;
	}

	return true;
}

bool obs_module_load(void)
{
	int version_major = atoi(obs_get_version_string());
	if (version_major && version_major < LIBOBS_API_MAJOR_VER) {
		blog(LOG_ERROR, "Cancel loading plugin since OBS version '%s' is older than plugin API version %d",
		     obs_get_version_string(), LIBOBS_API_MAJOR_VER);
		return false;
	}

#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(31, 0, 0)
	config_t *cfg = obs_frontend_get_global_config();
#else
	config_t *cfg = obs_frontend_get_app_config();
#endif
	config_set_default_bool(cfg, CONFIG_SECTION_NAME, "ShowSource", true);
	config_set_default_bool(cfg, CONFIG_SECTION_NAME, "ShowFilter", true);

	bool show_source = config_get_bool(cfg, CONFIG_SECTION_NAME, "ShowSource");
	uint32_t src_flags = show_source ? 0 : OBS_SOURCE_CAP_DISABLED;

	bool show_filter = config_get_bool(cfg, CONFIG_SECTION_NAME, "ShowFilter");
	uint32_t flt_flags = show_filter ? 0 : OBS_SOURCE_CAP_DISABLED;

	if (!register_source_with_flags(&colormonitor_vectorscope_v1, src_flags))
		return false;
	if (!register_source_with_flags(&colormonitor_vectorscope, src_flags))
		return false;
	if (!register_source_with_flags(&colormonitor_waveform, src_flags))
		return false;
	if (!register_source_with_flags(&colormonitor_histogram, src_flags))
		return false;
	if (!register_source_with_flags(&colormonitor_zebra, src_flags))
		return false;
	if (!register_source_with_flags(&colormonitor_zebra_filter, flt_flags))
		return false;
	if (!register_source_with_flags(&colormonitor_falsecolor, src_flags))
		return false;
	if (!register_source_with_flags(&colormonitor_falsecolor_filter, flt_flags))
		return false;
	if (!register_source_with_flags(&colormonitor_focuspeaking, src_flags))
		return false;
	if (!register_source_with_flags(&colormonitor_focuspeaking_filter, flt_flags))
		return false;
	if (!register_source_with_flags(&colormonitor_roi, src_flags))
		return false;

	scope_docks_init();
	blog(LOG_INFO, "plugin loaded (plugin version %s, API version %d.%d.%d)", PLUGIN_VERSION, LIBOBS_API_MAJOR_VER,
	     LIBOBS_API_MINOR_VER, LIBOBS_API_PATCH_VER);
	return true;
}
