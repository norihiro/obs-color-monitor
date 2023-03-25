#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void property_list_add_sources(obs_property_t *prop, obs_source_t *self);
obs_property_t *properties_add_colorspace(obs_properties_t *props, const char *name, const char *description);

int calc_colorspace(int);

gs_effect_t *create_effect_from_module_file(const char *basename);

#ifdef __cplusplus
}
#endif
