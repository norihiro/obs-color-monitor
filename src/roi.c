#include <obs-module.h>
#include <graphics/image-file.h>
#include <util/platform.h>
#include <util/darray.h>
#include <limits.h>
#include "plugin-macros.generated.h"
#include "obs-convenience.h"
#include "roi.h"

#define debug(format, ...)

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_render_name = "roi_render";
static const char *prof_convert_yuv_name = "convert_yuv";
static const char *prof_stage_surface_name = "stage_surface";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

// #define ENABLE_ROI_USER // uncomment if you want to use or debug

#define INTERACT_DRAW_ROI_RECT 1
#define INTERACT_DRAG_FIRST 2
#define INTERACT_DRAG_MOVE 4
#define INTERACT_DRAG_RESIZE 8
#define INTERACT_HANDLE_LO 0x010
#define INTERACT_HANDLE_RO 0x040
#define INTERACT_HANDLE_TO 0x100
#define INTERACT_HANDLE_BO 0x400
#define INTERACT_HANDLE_LI 0x020
#define INTERACT_HANDLE_RI 0x080
#define INTERACT_HANDLE_TI 0x200
#define INTERACT_HANDLE_BI 0x800

extern gs_effect_t *cm_rgb2yuv_effect;
static DARRAY(struct roi_source*) da_roi;
static pthread_mutex_t da_roi_mutex;

static const char *roi_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("ROI");
}

static void roi_update(void *, obs_data_t *);

static void *roi_create(obs_data_t *settings, obs_source_t *source)
{
	struct roi_source *src = bzalloc(sizeof(struct roi_source));

	src->cm.flags = CM_FLAG_ROI;
	cm_create(&src->cm, settings, source);

	src->x0 = -1;
	src->x1 = -1;
	src->y0 = -1;
	src->y1 = -1;
	src->x0in = -1;
	src->x1in = -1;
	src->y0in = -1;
	src->y1in = -1;

	roi_update(src, settings);

	pthread_mutex_lock(&da_roi_mutex);
	da_push_back(da_roi, &src);
	pthread_mutex_unlock(&da_roi_mutex);
	return src;
}

static void roi_destroy(void *data)
{
	struct roi_source *src = data;

	pthread_mutex_lock(&da_roi_mutex);
	da_erase_item(da_roi, &src);
	pthread_mutex_unlock(&da_roi_mutex);

	cm_destroy(&src->cm);
	bfree(src);
}

static void roi_update(void *data, obs_data_t *settings)
{
	struct roi_source *src = data;
	cm_update(&src->cm, settings);

	src->n_interleave = (int)obs_data_get_int(settings, "interleave");
}

static void roi_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "target_scale", 2);
	obs_data_set_default_int(settings, "interleave", 1);
}

static obs_properties_t *roi_get_properties(void *data)
{
	struct roi_source *src = data;
	obs_properties_t *props = obs_properties_create();
	cm_get_properties(&src->cm, props);

	obs_properties_add_int(props, "interleave", obs_module_text("Interleave"), 0, 1, 1);

	return props;
}

static uint32_t roi_get_width(void *data)
{
	struct roi_source *src = data;
	return src->cm.known_width;
}

static uint32_t roi_get_height(void *data)
{
	struct roi_source *src = data;
	return src->cm.known_height;
}

static inline int min_int(int a, int b) { return a<b ? a : b; }
static inline int max_int(int a, int b) { return a>b ? a : b; }
static inline void swap_int (int *a, int *b) { int c=*a; *a=*b; *b=c; }

static inline int handle_size(const struct roi_source *src)
{
	const int wh_min = min_int(src->cm.known_width, src->cm.known_height);
	return wh_min / 12;
}

static inline bool handle_is_outside_x(const struct roi_source *src, int x0, int x1, uint32_t flags)
{
	const int wh_min = min_int(src->cm.known_width, src->cm.known_height);
	int size_th = wh_min / 3;
	if (flags & (INTERACT_HANDLE_LO | INTERACT_HANDLE_RO))
		return true;
	if (flags & (INTERACT_HANDLE_LI | INTERACT_HANDLE_RI))
		return false;
	if (x1 - x0 <= size_th)
		return true;
	return false;
}

static inline bool handle_is_outside_y(const struct roi_source *src, int x0, int x1, uint32_t flags)
{
	const int wh_min = min_int(src->cm.known_width, src->cm.known_height);
	int size_th = wh_min / 3;
	if (flags & (INTERACT_HANDLE_TO | INTERACT_HANDLE_BO))
		return true;
	if (flags & (INTERACT_HANDLE_TI | INTERACT_HANDLE_BI))
		return false;
	if (x1 - x0 <= size_th)
		return true;
	return false;
}

static inline void draw_add_handle_x(int xh, int x, int y0, int y1, bool outside)
{
	gs_vertex2f((float)xh, (float)y0); gs_vertex2f((float)xh, (float)y1);
	if (outside) {
		gs_vertex2f((float)xh, (float)y0); gs_vertex2f((float)x, (float)y0);
		gs_vertex2f((float)xh, (float)y1); gs_vertex2f((float)x, (float)y1);
	}
}

static inline void draw_add_handle_y(int x0, int x1, int yh, int y, bool outside)
{
	gs_vertex2f((float)x0, (float)yh); gs_vertex2f((float)x1, (float)yh);
	if (outside) {
		gs_vertex2f((float)x0, (float)yh); gs_vertex2f((float)x0, (float)y);
		gs_vertex2f((float)x1, (float)yh); gs_vertex2f((float)x1, (float)y);
	}
}

static inline void draw_roi_rect(const struct roi_source *src, int x0, int y0, int x1, int y1, uint32_t flags)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_effect_set_color(gs_effect_get_param_by_name(effect, "color"), 0xFF00FF00);
	const int hh = handle_size(src);
	const bool x_outside = handle_is_outside_x(src, x0, x1, flags);
	const bool y_outside = handle_is_outside_y(src, y0, y1, flags);
	const int x0h = x_outside ? x0 - hh : x0 + hh;
	const int x1h = x_outside ? x1 + hh : x1 - hh;
	const int y0h = y_outside ? y0 - hh : y0 + hh;
	const int y1h = y_outside ? y1 + hh : y1 - hh;
	const int x0e = x_outside ? x0 : x0h;
	const int x1e = x_outside ? x1 : x1h;
	const int y0e = y_outside ? y0 : y0h;
	const int y1e = y_outside ? y1 : y1h;
	while (gs_effect_loop(effect, "Solid")) {
		gs_render_start(false);
		gs_vertex2f((float)x0, (float)y1); gs_vertex2f((float)x0, (float)y0);
		gs_vertex2f((float)x0, (float)y0); gs_vertex2f((float)x1, (float)y0);
		gs_vertex2f((float)x1, (float)y0); gs_vertex2f((float)x1, (float)y1);
		gs_vertex2f((float)x1, (float)y1); gs_vertex2f((float)x0, (float)y1);
		if (flags & (INTERACT_HANDLE_LI | INTERACT_HANDLE_LO))
			draw_add_handle_x(x0h, x0, y0e, y1e, !y_outside || x_outside);
		if (flags & (INTERACT_HANDLE_RI | INTERACT_HANDLE_RO))
			draw_add_handle_x(x1h, x1, y0e, y1e, !y_outside || x_outside);
		if (flags & (INTERACT_HANDLE_TI | INTERACT_HANDLE_TO))
			draw_add_handle_y(x0e, x1e, y0h, y0, !x_outside || y_outside);
		if (flags & (INTERACT_HANDLE_BI | INTERACT_HANDLE_BO))
			draw_add_handle_y(x0e, x1e, y1h, y1, !x_outside || y_outside);
		gs_render_stop(GS_LINES);
	}
}

static inline void draw_roi_range(const struct roi_source *src, float x0, float y0, float x1, float y1)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_effect_set_color(gs_effect_get_param_by_name(effect, "color"), 0x80000000);
	while (gs_effect_loop(effect, "Solid")) {
		float w = (float)src->cm.known_width;
		float h = (float)src->cm.known_height;
		gs_render_start(false);
		gs_vertex2f(x0, y1);
		gs_vertex2f(0.0f, h);
		gs_vertex2f(x0, y0);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertex2f(x1, y0);
		gs_vertex2f(w, 0.0f);
		gs_vertex2f(x1, y1);
		gs_vertex2f(w, h);
		gs_vertex2f(x0, y1);
		gs_vertex2f(0.0f, h);
		gs_render_stop(GS_TRISTRIP);
	}
}

static inline gs_stagesurf_t *resize_stagesurface(gs_stagesurf_t *stagesurface, int width, int height)
{
	if (
			!stagesurface ||
			width != gs_stagesurface_get_width(stagesurface) ||
			height != gs_stagesurface_get_height(stagesurface) ) {
		gs_stagesurface_destroy(stagesurface);
		stagesurface = gs_stagesurface_create(width, height, GS_BGRA);
	}
	return stagesurface;
}

static inline void set_roi_info(struct roi_surface_info_s *pos, struct roi_source *src)
{
	if (0 <= src->x0 && src->x0 <= src->x1 && src->x1 <= src->cm.known_width) {
		pos->x0 = src->x0;
		pos->w = src->x1 - src->x0;
	}
	else {
		pos->x0 = 0;
		pos->w = src->cm.known_width;
	}
	if (0 <= src->y0 && src->y0 <= src->y1 && src->y1 <= src->cm.known_height) {
		pos->y0 = src->y0;
		pos->h = src->y1 - src->y0;
	}
	else {
		pos->y0 = 0;
		pos->h = src->cm.known_height;
	}
}

static void roi_stage_texture(struct roi_source *src)
{
	const bool b_rgb = src->b_rgb;
	const bool b_yuv = src->b_yuv;

	if (!b_rgb && !b_yuv)
		return;

	const int height0 = src->cm.known_height;
	const int height = height0 * (b_rgb && b_yuv ? 2 : 1);
	const int width = src->cm.known_width;

	src->cm.stagesurface = resize_stagesurface(src->cm.stagesurface, width, height);

	if (b_rgb && !b_yuv) {
		PROFILE_START(prof_stage_surface_name);
		gs_stage_texture(src->cm.stagesurface, gs_texrender_get_texture(src->cm.texrender));
		PROFILE_END(prof_stage_surface_name);
		return;
	}

	gs_texrender_reset(src->cm.texrender_yuv);
	if (!gs_texrender_begin(src->cm.texrender_yuv, width, height)) {
		blog(LOG_ERROR, "colormonitor_roi: gs_texrender_begin failed %p %d %d", src->cm.texrender_yuv, width, height);
		return;
	}

	struct vec4 background;
	vec4_zero(&background);
	gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

	if (b_rgb) {
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_texture_t *tex = gs_texrender_get_texture(src->cm.texrender);
		if (tex) {
			gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
			while (gs_effect_loop(effect, "Draw"))
				gs_draw_sprite(tex, 0, width, height0);
		}

		// YUV texture will be drawn on bottom
		gs_matrix_translate3f(0.0f, (float)height0, 0.0f);
	}

	gs_texture_t *tex = gs_texrender_get_texture(src->cm.texrender);
	if (cm_rgb2yuv_effect && tex) {
		PROFILE_START(prof_convert_yuv_name);
		gs_effect_set_texture(gs_effect_get_param_by_name(cm_rgb2yuv_effect, "image"), tex);
		while (gs_effect_loop(cm_rgb2yuv_effect, src->cm.colorspace==1 ? "ConvertRGB_UV601" : "ConvertRGB_UV709"))
			gs_draw_sprite(tex, 0, width, height0);
		PROFILE_END(prof_convert_yuv_name);
	}

	gs_blend_state_pop();
	gs_texrender_end(src->cm.texrender_yuv);

	PROFILE_START(prof_stage_surface_name);
	gs_stage_texture(src->cm.stagesurface, gs_texrender_get_texture(src->cm.texrender_yuv));
	PROFILE_END(prof_stage_surface_name);
	set_roi_info(&src->roi_surface_pos_next, src);
	src->roi_surface_pos_next.surface_height = height;
	src->roi_surface_pos_next.b_rgb = src->b_rgb;
	src->roi_surface_pos_next.b_yuv = src->b_yuv;
}

bool roi_target_render(struct roi_source *src)
{
	if (src->i_interleave!=0 && src->n_interleave>0)
		return true;

	bool updated = cm_render_target(&src->cm);
	if (updated)
		roi_stage_texture(src);

	if (src->n_interleave<=0)
		return true;
	return false;
}

static void roi_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct roi_source *src = data;

	PROFILE_START(prof_render_name);

	roi_target_render(src);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_texture_t *tex = gs_texrender_get_texture(src->cm.texrender);
	if (effect && tex) {
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite(tex, 0, src->cm.known_width, src->cm.known_height);
		}
	}

	draw_roi_range(src, (float)src->x0, (float)src->y0, (float)src->x1, (float)src->y1);

	uint32_t flags_interact = src->flags_interact_gs;
	if (flags_interact & (INTERACT_DRAG_RESIZE | INTERACT_DRAG_FIRST))
		draw_roi_rect(src, src->x0sizing, src->y0sizing, src->x1sizing, src->y1sizing, flags_interact);
	else if (flags_interact & INTERACT_DRAW_ROI_RECT)
		draw_roi_rect(src, src->x0, src->y0, src->x1, src->y1, flags_interact);

	gs_blend_state_pop();

	PROFILE_END(prof_render_name);
}

bool roi_stagesurfae_map(struct roi_source *src, uint8_t **video_data, uint32_t *video_linesize, int ix)
{
	if (ix && !src->roi_surface_pos.b_yuv) {
		blog(LOG_INFO, "roi_stagesurfae_map: YUV frame is not staged");
		return false;
	}
	if (!ix && !src->roi_surface_pos.b_rgb) {
		blog(LOG_INFO, "roi_stagesurfae_map: RGB frame is not staged");
		return false;
	}
	bool ret = gs_stagesurface_map(src->cm.stagesurface, video_data, video_linesize);
	int x0 = src->roi_surface_pos.x0;
	int y0 = src->roi_surface_pos.y0;
	if (x0 > 0)
		*video_data += 4 * x0;
	if (y0 > 0)
		*video_data += *video_linesize * y0;
	if (ix && src->roi_surface_pos.b_rgb && src->roi_surface_pos.b_yuv)
		*video_data += *video_linesize * src->cm.known_height;
	return ret;
}

void roi_stagesurfae_unmap(struct roi_source *src)
{
	gs_stagesurface_unmap(src->cm.stagesurface);
}

static uint32_t handle_from_pos(struct roi_source *src, int x, int y)
{
	const int hh = handle_size(src);
	bool x_inside = false, y_inside = false;

	uint32_t flags = 0;

	if (handle_is_outside_x(src, src->x0in, src->x1in, 0)) {
		if (src->x0in - hh <= x && x <= src->x0in)
			flags |= INTERACT_HANDLE_LO;
		if (src->x1in <= x && x <= src->x1in + hh)
			flags |= INTERACT_HANDLE_RO;
		if (src->x0in - hh <= x && x <= src->x1in + hh)
			x_inside = true;
	}
	else {
		if (src->x0in <= x && x <= src->x0in + hh)
			flags |= INTERACT_HANDLE_LI;
		if (src->x1in - hh <= x && x <= src->x1in)
			flags |= INTERACT_HANDLE_RI;
		if (src->x0in <= x && x <= src->x1in)
			x_inside = true;
	}

	if (handle_is_outside_y(src, src->y0in, src->y1in, 0)) {
		if (src->y0in - hh <= y && y <= src->y0in)
			flags |= INTERACT_HANDLE_TO;
		if (src->y1in <= y && y <= src->y1in + hh)
			flags |= INTERACT_HANDLE_BO;
		if (src->y0in - hh <= y && y <= src->y1in + hh)
			y_inside = true;
	}
	else {
		if (src->y0in <= y && y <= src->y0in + hh)
			flags |= INTERACT_HANDLE_TI;
		if (src->y1in - hh <= y && y <= src->y1in)
			flags |= INTERACT_HANDLE_BI;
		if (src->y0in <= y && y <= src->y1in)
			y_inside = true;
	}

	if (!x_inside)
		flags &= ~INTERACT_HANDLE_TO & ~INTERACT_HANDLE_BO & ~INTERACT_HANDLE_TI & ~INTERACT_HANDLE_BI;
	if (!y_inside)
		flags &= ~INTERACT_HANDLE_LO & ~INTERACT_HANDLE_RO & ~INTERACT_HANDLE_LI & ~INTERACT_HANDLE_RI;
	if (x_inside && y_inside)
		flags |= INTERACT_DRAW_ROI_RECT;

	return flags;
}

static inline void drag_move_pos(struct roi_source *src, int dx, int dy)
{
	src->x0in += dx;
	src->y0in += dy;
	src->x1in += dx;
	src->y1in += dy;
}

static void roi_mouse_move(void *data, const struct obs_mouse_event *event, bool mouse_leave)
{
	struct roi_source *src = data;
	if (mouse_leave) {
		src->x_start = INT_MIN;
		src->y_start = INT_MIN;
		src->flags_interact = 0;
		return;
	}

	int x = event->x;
	int y = event->y;
	src->x_mouse = x;
	src->y_mouse = y;

	if (src->x_start == INT_MIN && src->y_start == INT_MIN) {
		src->flags_interact = handle_from_pos(src, x, y);
	}
	else if (src->x_start!=INT_MIN && src->y_start != INT_MIN) {
		if (src->flags_interact & INTERACT_DRAG_MOVE) {
			drag_move_pos(src, x - src->x_start, y - src->y_start);
			src->x_start = x;
			src->y_start = y;
		}
	}
}

static inline bool is_resize(uint32_t flags)
{
	if (flags & (INTERACT_HANDLE_LO | INTERACT_HANDLE_LI)) return true;
	if (flags & (INTERACT_HANDLE_RO | INTERACT_HANDLE_RI)) return true;
	if (flags & (INTERACT_HANDLE_TO | INTERACT_HANDLE_TI)) return true;
	if (flags & (INTERACT_HANDLE_BO | INTERACT_HANDLE_BI)) return true;
	return false;
}

static void roi_mouse_click(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count)
{
	struct roi_source *src = data;

	src->x_mouse = event->x;
	src->y_mouse = event->y;

	if (mouse_up && (src->flags_interact & INTERACT_DRAG_FIRST)) {
		if (src->x_start==event->x || src->y_start==event->y) {
			src->x0in = -1;
			src->x1in = -1;
			src->y0in = -1;
			src->y1in = -1;
		}
		else {
			src->x0in = min_int(src->x_start, event->x);
			src->y0in = min_int(src->y_start, event->y);
			src->x1in = max_int(src->x_start, event->x);
			src->y1in = max_int(src->y_start, event->y);
		}
		src->x_start = INT_MIN;
		src->y_start = INT_MIN;
		src->flags_interact = 0;
	}
	else if (!mouse_up) {
		src->x_start = event->x;
		src->y_start = event->y;
		if (src->flags_interact & INTERACT_DRAW_ROI_RECT) {
			if (is_resize(src->flags_interact))
				src->flags_interact |= INTERACT_DRAG_RESIZE;
			else
				src->flags_interact |= INTERACT_DRAG_MOVE;
		}
		else
			src->flags_interact |= INTERACT_DRAG_FIRST;
	}
	else if (mouse_up && (src->flags_interact & INTERACT_DRAG_MOVE)) {
		src->x_start = INT_MIN;
		src->y_start = INT_MIN;
		src->flags_interact &= ~INTERACT_DRAG_MOVE;
	}
	else if (mouse_up && (src->flags_interact & INTERACT_DRAG_RESIZE)) {
		if (src->flags_interact & (INTERACT_HANDLE_LO | INTERACT_HANDLE_LI))
			src->x0in += event->x - src->x_start;
		if (src->flags_interact & (INTERACT_HANDLE_RO | INTERACT_HANDLE_RI))
			src->x1in += event->x - src->x_start;
		if (src->flags_interact & (INTERACT_HANDLE_TO | INTERACT_HANDLE_TI))
			src->y0in += event->y - src->y_start;
		if (src->flags_interact & (INTERACT_HANDLE_BO | INTERACT_HANDLE_BI))
			src->y1in += event->y - src->y_start;
		if (src->x0in > src->x1in) swap_int(&src->x0in, &src->x1in);
		if (src->y0in > src->y1in) swap_int(&src->y0in, &src->y1in);
		src->x_start = INT_MIN;
		src->y_start = INT_MIN;
		src->flags_interact &= ~INTERACT_DRAG_RESIZE;
	}
}

static void roi_send_range(struct roi_source *src)
{
	uint32_t flags_interact = src->flags_interact;
	src->flags_interact_gs = flags_interact;
	src->x0 = src->x0in;
	src->y0 = src->y0in;
	src->x1 = src->x1in;
	src->y1 = src->y1in;
	if (src->x0<0) src->x0 = 0;
	if (src->x1<0 || (int)src->cm.known_width < src->x1)
		src->x1 = src->cm.known_width;
	if (src->y0<0) src->y0 = 0;
	if (src->y1<0 || (int)src->cm.known_height < src->y1)
		src->y1 = src->cm.known_height;

	if (flags_interact & INTERACT_DRAG_FIRST) {
		src->x0sizing = min_int(src->x_start, src->x_mouse);
		src->y0sizing = min_int(src->y_start, src->y_mouse);
		src->x1sizing = max_int(src->x_start, src->x_mouse);
		src->y1sizing = max_int(src->y_start, src->y_mouse);
	}
	if (flags_interact & INTERACT_DRAG_RESIZE) {
		src->x0sizing = src->x0in;
		src->y0sizing = src->y0in;
		src->x1sizing = src->x1in;
		src->y1sizing = src->y1in;
		if (flags_interact & (INTERACT_HANDLE_LO | INTERACT_HANDLE_LI))
			src->x0sizing += src->x_mouse - src->x_start;
		if (flags_interact & (INTERACT_HANDLE_RO | INTERACT_HANDLE_RI))
			src->x1sizing += src->x_mouse - src->x_start;
		if (flags_interact & (INTERACT_HANDLE_TO | INTERACT_HANDLE_TI))
			src->y0sizing += src->y_mouse - src->y_start;
		if (flags_interact & (INTERACT_HANDLE_BO | INTERACT_HANDLE_BI))
			src->y1sizing += src->y_mouse - src->y_start;
	}
}

static void roi_tick(void *data, float unused)
{
	struct roi_source *src = data;

	if (src->i_interleave++ >= src->n_interleave)
		src->i_interleave = 0;

	if (src->i_interleave==0 || src->n_interleave<=0)
		cm_tick(data, unused);

	src->b_rgb = src->n_rgb > 0;
	src->b_yuv = (src->n_uv > 0 || src->n_y > 0);

	if (src->n_rgb > 0)
		src->n_rgb --;
	if (src->n_uv > 0)
		src->n_uv --;
	if (src->n_y > 0)
		src->n_y --;

	roi_send_range(src);

	if (src->i_interleave!=0 && src->n_interleave>0)
		src->roi_surface_pos = src->roi_surface_pos_next;
}

struct roi_source *roi_from_source(obs_source_t *s)
{
	struct roi_source *ret = NULL;
	pthread_mutex_lock(&da_roi_mutex);
	for (size_t i=0; i<da_roi.num; i++) {
		if (da_roi.array[i]->cm.self == s) {
			ret = da_roi.array[i];
			break;
		}
	}
	pthread_mutex_unlock(&da_roi_mutex);
	return ret;
}

bool is_roi_source_name(const char *name)
{
	obs_source_t *src = obs_get_source_by_name(name);
	if (!src)
		return false;
	struct roi_source *roi = roi_from_source(src);
	obs_source_release(src);
	return !!roi;
}

struct obs_source_info colormonitor_roi = {
	.id = "colormonitor_roi",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags =
#ifndef ENABLE_ROI_USER
		OBS_SOURCE_CAP_DISABLED |
#endif
		OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION,
	.get_name = roi_get_name,
	.create = roi_create,
	.destroy = roi_destroy,
	.update = roi_update,
	.get_defaults = roi_get_defaults,
	.get_properties = roi_get_properties,
	.get_width = roi_get_width,
	.get_height = roi_get_height,
	.enum_active_sources = cm_enum_sources,
	.video_render = roi_render,
	.video_tick = roi_tick,
	.mouse_move = roi_mouse_move,
	.mouse_click = roi_mouse_click,
};

void roi_init()
{
	pthread_mutex_init(&da_roi_mutex, NULL);
	da_init(da_roi);
}

void roi_free()
{
	pthread_mutex_lock(&da_roi_mutex);
	if (da_roi.num>0)
		blog(LOG_ERROR, "da_roi has %d element(s)", (int)da_roi.num);
	da_free(da_roi);
	pthread_mutex_unlock(&da_roi_mutex);
	pthread_mutex_destroy(&da_roi_mutex);
}
