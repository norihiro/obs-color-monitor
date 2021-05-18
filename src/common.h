#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void property_list_add_sources(obs_property_t *prop, obs_source_t *self);

static inline bool is_preview_name(const char *name)
{
	return *name && name[0]==0x10 && name[1]==0;
}

#ifdef __cplusplus
}
#endif
