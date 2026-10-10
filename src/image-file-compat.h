#pragma once
#include <obs-module.h>
#include <graphics/image-file.h>

#if LIBOBS_API_MAJOR_VER >= 33
typedef gs_image_file_ex_t colormonitor_image_t;
#define colormonitor_image_free gs_image_file_ex_free
#define colormonitor_image_init_texture gs_image_file_ex_init_texture
static inline void colormonitor_image_init(colormonitor_image_t *image, const char *file)
{
	gs_image_file_ex_init(image, file, GS_IMAGE_ALPHA_STRAIGHT);
}
#else
typedef gs_image_file_t colormonitor_image_t;
#define colormonitor_image_free gs_image_file_free
#define colormonitor_image_init_texture gs_image_file_init_texture
#define colormonitor_image_init gs_image_file_init
#endif
