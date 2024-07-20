#include <obs-module.h>
#include <util/platform.h>
#include <util/darray.h>
#include <limits.h>
#include "plugin-macros.generated.h"
#include "obs-convenience.h"
#include "roi.h"
#include "common.h"
#include "util.h"

#ifdef ENABLE_PROFILE
#define PROFILE_START(x) profile_start(x)
#define PROFILE_END(x) profile_end(x)
static const char *prof_render_name = "roi_render";
#else // ENABLE_PROFILE
#define PROFILE_START(x)
#define PROFILE_END(x)
#endif // ! ENABLE_PROFILE

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
#define INTERACT_HANDLE_LR_ANY (INTERACT_HANDLE_LO | INTERACT_HANDLE_RO | INTERACT_HANDLE_LI | INTERACT_HANDLE_RI)
#define INTERACT_HANDLE_TB_ANY (INTERACT_HANDLE_TO | INTERACT_HANDLE_BO | INTERACT_HANDLE_TI | INTERACT_HANDLE_BI)

#define ROI_DEFAULT_CM_FLAG (CM_FLAG_ROI | CM_FLAG_RAW_TEXTURE)

static void roi_update(void *, obs_data_t *);
static void roi_surface_cb(void *data, struct cm_surface_data *surface_data);

static const char *roi_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("ROI");
}

static void cb_get_roi(void *data, calldata_t *cd)
{
	calldata_set_ptr(cd, "roi", data);
}

static void *roi_create(obs_data_t *settings, obs_source_t *source)
{
	struct roi_source *src = bzalloc(sizeof(struct roi_source));

	pthread_mutex_init(&src->sources_mutex, NULL);

	src->cm.flags = ROI_DEFAULT_CM_FLAG;
	cm_create(&src->cm, settings, source);
	cm_request(&src->cm, roi_surface_cb, src);

	src->cm.x0 = -1;
	src->cm.x1 = -1;
	src->cm.y0 = -1;
	src->cm.y1 = -1;
	src->x0in = -1;
	src->x1in = -1;
	src->y0in = -1;
	src->y1in = -1;

	roi_update(src, settings);

	proc_handler_t *ph = obs_source_get_proc_handler(source);
	proc_handler_add(ph, "void get_roi(out ptr roi)", cb_get_roi, src);
	return src;
}

static void roi_destroy(void *data)
{
	struct roi_source *src = data;

	cm_destroy(&src->cm);
	da_free(src->sources);
	pthread_mutex_destroy(&src->sources_mutex);

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
	properties_add_colorspace(props, "colorspace", obs_module_text("Color space"));

	return props;
}

static inline uint32_t roi_get_width(const struct roi_source *src)
{
	return src->cm.texrender_width;
}

static inline uint32_t roi_get_height(const struct roi_source *src)
{
	return src->cm.texrender_height;
}

static uint32_t roi_get_width_1(void *data)
{
	return roi_get_width(data);
}

static uint32_t roi_get_height_1(void *data)
{
	return roi_get_height(data);
}

static inline int min_int(int a, int b)
{
	return a < b ? a : b;
}
static inline int max_int(int a, int b)
{
	return a > b ? a : b;
}
static inline void swap_int(int *a, int *b)
{
	int c = *a;
	*a = *b;
	*b = c;
}

static inline int handle_size(const struct roi_source *src)
{
	uint32_t w = roi_get_width(src);
	uint32_t h = roi_get_height(src);
	const int wh_min = min_int(w, h);
	return wh_min / 12;
}

static bool handle_is_outside(const struct roi_source *src, int x0, int x1)
{
	const int wh_min = min_int(roi_get_width(src), roi_get_height(src));
	if (x1 - x0 <= wh_min / 3)
		return true;
	return false;
}

static inline bool handle_is_outside_x(const struct roi_source *src, int x0, int x1, uint32_t flags)
{
	if (flags & (INTERACT_HANDLE_LO | INTERACT_HANDLE_RO))
		return true;
	if (flags & (INTERACT_HANDLE_LI | INTERACT_HANDLE_RI))
		return false;
	return handle_is_outside(src, x0, x1);
}

static inline bool handle_is_outside_y(const struct roi_source *src, int x0, int x1, uint32_t flags)
{
	if (flags & (INTERACT_HANDLE_TO | INTERACT_HANDLE_BO))
		return true;
	if (flags & (INTERACT_HANDLE_TI | INTERACT_HANDLE_BI))
		return false;
	return handle_is_outside(src, x0, x1);
}

static inline void draw_add_handle_x(int xh, int x, int y0, int y1, bool outside)
{
	gs_vertex2f((float)xh, (float)y0);
	gs_vertex2f((float)xh, (float)y1);
	if (outside) {
		gs_vertex2f((float)xh, (float)y0);
		gs_vertex2f((float)x, (float)y0);
		gs_vertex2f((float)xh, (float)y1);
		gs_vertex2f((float)x, (float)y1);
	}
}

static inline void draw_add_handle_y(int x0, int x1, int yh, int y, bool outside)
{
	gs_vertex2f((float)x0, (float)yh);
	gs_vertex2f((float)x1, (float)yh);
	if (outside) {
		gs_vertex2f((float)x0, (float)yh);
		gs_vertex2f((float)x0, (float)y);
		gs_vertex2f((float)x1, (float)yh);
		gs_vertex2f((float)x1, (float)y);
	}
}

static inline void draw_roi_rect(struct roi_source *src, int x0, int y0, int x1, int y1, uint32_t flags)
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
		gs_vertex2f((float)x0, (float)y1);
		gs_vertex2f((float)x0, (float)y0);
		gs_vertex2f((float)x0, (float)y0);
		gs_vertex2f((float)x1, (float)y0);
		gs_vertex2f((float)x1, (float)y0);
		gs_vertex2f((float)x1, (float)y1);
		gs_vertex2f((float)x1, (float)y1);
		gs_vertex2f((float)x0, (float)y1);
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
		float w = (float)roi_get_width(src);
		float h = (float)roi_get_height(src);
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

bool roi_target_render(struct roi_source *src)
{
	src->interleave_rendered = true;
	if (src->i_interleave != 0 && src->n_interleave > 0)
		return true;

	cm_render_target(&src->cm);

	if (src->n_interleave <= 0)
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

	cm_render_target(&src->cm);

	gs_texture_t *tex = gs_texrender_get_texture(src->cm.texrender);
	if (tex) {
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite(tex, 0, src->cm.texrender_width, src->cm.texrender_height);
		}
	}

	draw_roi_range(src, (float)src->cm.x0, (float)src->cm.y0, (float)src->cm.x1, (float)src->cm.y1);

	uint32_t flags_interact = src->flags_interact_gs;
	if (flags_interact & (INTERACT_DRAG_RESIZE | INTERACT_DRAG_FIRST))
		draw_roi_rect(src, src->x0sizing, src->y0sizing, src->x1sizing, src->y1sizing, flags_interact);
	else if (flags_interact & INTERACT_DRAW_ROI_RECT)
		draw_roi_rect(src, src->cm.x0, src->cm.y0, src->cm.x1, src->cm.y1, flags_interact);

	gs_blend_state_pop();

	PROFILE_END(prof_render_name);
}

void roi_register_source(struct roi_source *src, struct cm_source *cm)
{
	pthread_mutex_lock(&src->sources_mutex);
	da_push_back(src->sources, &cm);
	pthread_mutex_unlock(&src->sources_mutex);
}

void roi_unregister_source(struct roi_source *src, struct cm_source *cm)
{
	pthread_mutex_lock(&src->sources_mutex);
	da_erase_item(src->sources, &cm);
	pthread_mutex_unlock(&src->sources_mutex);
}

static void roi_surface_cb(void *data, struct cm_surface_data *surface_data)
{
	struct roi_source *src = data;

	pthread_mutex_lock(&src->sources_mutex);
	for (size_t i = 0; i < src->sources.num; i++) {
		struct cm_source *cm = src->sources.array[i];
		if (cm->callback) {
			cm->callback(cm->callback_data, surface_data);
		}
	}
	pthread_mutex_unlock(&src->sources_mutex);
}

static uint32_t make_flags_from_mouse(struct roi_source *src, int x0in, int x1in, int x, uint32_t flag_base,
				      uint32_t flag_x_inside)
{
	const int hh = handle_size(src);
	uint32_t flags = 0;

	if (handle_is_outside(src, x0in, x1in)) {
		if (x0in - hh <= x && x <= x0in)
			flags |= flag_base; // INTERACT_HANDLE_LO;
		if (x1in <= x && x <= x1in + hh)
			flags |= flag_base << 2; // INTERACT_HANDLE_RO;
		if (x0in - hh <= x && x <= x1in + hh)
			flags |= flag_x_inside;
	} else {
		if (x0in <= x && x <= x0in + hh)
			flags |= flag_base << 1; // INTERACT_HANDLE_LI;
		if (x1in - hh <= x && x <= x1in)
			flags |= flag_base << 3; // INTERACT_HANDLE_RI;
		if (x0in <= x && x <= x1in)
			flags |= flag_x_inside;
	}

	return flags;
}

static uint32_t handle_from_pos(struct roi_source *src, int x, int y)
{
	uint32_t fx = make_flags_from_mouse(src, src->x0in, src->x1in, x, INTERACT_HANDLE_LO,
					    INTERACT_HANDLE_TB_ANY | INTERACT_DRAW_ROI_RECT);
	uint32_t fy = make_flags_from_mouse(src, src->y0in, src->y1in, y, INTERACT_HANDLE_TO,
					    INTERACT_HANDLE_LR_ANY | INTERACT_DRAW_ROI_RECT);
	return fx & fy;
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
	} else if (src->x_start != INT_MIN && src->y_start != INT_MIN) {
		if (src->flags_interact & INTERACT_DRAG_MOVE) {
			drag_move_pos(src, x - src->x_start, y - src->y_start);
			src->x_start = x;
			src->y_start = y;
		}
	}
}

static void roi_mouse_click_start(struct roi_source *src)
{
	src->x_start = src->x_mouse;
	src->y_start = src->y_mouse;
	if (src->flags_interact & (INTERACT_HANDLE_LR_ANY | INTERACT_HANDLE_TB_ANY))
		src->flags_interact |= INTERACT_DRAG_RESIZE;
	else if (src->flags_interact & INTERACT_DRAW_ROI_RECT)
		src->flags_interact |= INTERACT_DRAG_MOVE;
	else
		src->flags_interact |= INTERACT_DRAG_FIRST;
}

static void roi_mouse_drag_wo_roi_end(struct roi_source *src)
{
	bool ok = src->x_start != src->x_mouse && src->y_start != src->y_mouse;

	src->x0in = ok ? min_int(src->x_start, src->x_mouse) : -1;
	src->y0in = ok ? min_int(src->y_start, src->y_mouse) : -1;
	src->x1in = ok ? max_int(src->x_start, src->x_mouse) : -1;
	src->y1in = ok ? max_int(src->y_start, src->y_mouse) : -1;
}

static void roi_mouse_drag_resize_end(struct roi_source *src)
{
	if (src->flags_interact & (INTERACT_HANDLE_LO | INTERACT_HANDLE_LI))
		src->x0in += src->x_mouse - src->x_start;
	if (src->flags_interact & (INTERACT_HANDLE_RO | INTERACT_HANDLE_RI))
		src->x1in += src->x_mouse - src->x_start;
	if (src->flags_interact & (INTERACT_HANDLE_TO | INTERACT_HANDLE_TI))
		src->y0in += src->y_mouse - src->y_start;
	if (src->flags_interact & (INTERACT_HANDLE_BO | INTERACT_HANDLE_BI))
		src->y1in += src->y_mouse - src->y_start;

	if (src->x0in > src->x1in)
		swap_int(&src->x0in, &src->x1in);
	if (src->y0in > src->y1in)
		swap_int(&src->y0in, &src->y1in);
}

static void roi_mouse_click(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up,
			    uint32_t click_count)
{
	struct roi_source *src = data;

	UNUSED_PARAMETER(click_count);

	if (type != MOUSE_LEFT)
		return;

	src->x_mouse = event->x;
	src->y_mouse = event->y;

	if (!mouse_up) {
		roi_mouse_click_start(src);
		return;
	}

	if ((src->flags_interact & INTERACT_DRAG_FIRST))
		roi_mouse_drag_wo_roi_end(src);
	else if ((src->flags_interact & INTERACT_DRAG_RESIZE))
		roi_mouse_drag_resize_end(src);

	src->x_start = INT_MIN;
	src->y_start = INT_MIN;
	src->flags_interact &= ~INTERACT_DRAG_FIRST & ~INTERACT_DRAG_MOVE & ~INTERACT_DRAG_RESIZE;
}

static void roi_send_range(struct roi_source *src)
{
	int w = (int)roi_get_width(src);
	int h = (int)roi_get_height(src);
	uint32_t flags_interact = src->flags_interact;
	src->flags_interact_gs = flags_interact;
	int x0 = src->x0in;
	int y0 = src->y0in;
	int x1 = src->x1in;
	int y1 = src->y1in;
	if (x0 < 0)
		x0 = 0;
	if (x1 < 0 || w < x1)
		x1 = w;
	if (y0 < 0)
		y0 = 0;
	if (y1 < 0 || h < y1)
		y1 = h;
	src->cm.x0 = x0;
	src->cm.y0 = y0;
	src->cm.y1 = y1;
	src->cm.x1 = x1;

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

	if (src->interleave_rendered && src->i_interleave++ >= src->n_interleave)
		src->i_interleave = 0;
	src->interleave_rendered = false;

	if (src->i_interleave == 0 || src->n_interleave <= 0)
		cm_tick(data, unused);

	src->cm.flags = ROI_DEFAULT_CM_FLAG;
	pthread_mutex_lock(&src->sources_mutex);
	for (size_t i = 0; i < src->sources.num; i++) {
		struct cm_source *cm = src->sources.array[i];
		src->cm.flags |= cm->flags & (CM_FLAG_CONVERT_RGB | CM_FLAG_CONVERT_YUV);
	}
	pthread_mutex_unlock(&src->sources_mutex);

	roi_send_range(src);
}

struct roi_source *roi_from_source(obs_source_t *s)
{
	proc_handler_t *ph = obs_source_get_proc_handler(s);
	if (!ph)
		return NULL;

	struct roi_source *ret = NULL;

	calldata_t cd = {0};
	uint8_t stack[128];
	calldata_init_fixed(&cd, stack, sizeof(stack));
	proc_handler_call(ph, "get_roi", &cd);
	calldata_get_ptr(&cd, "roi", &ret);

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

const struct obs_source_info colormonitor_roi = {
	.id = "colormonitor_roi",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags =
#ifndef SHOW_ROI
		OBS_SOURCE_CAP_DISABLED |
#endif
		OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION,
	.get_name = roi_get_name,
	.create = roi_create,
	.destroy = roi_destroy,
	.update = roi_update,
	.get_defaults = roi_get_defaults,
	.get_properties = roi_get_properties,
	.get_width = roi_get_width_1,
	.get_height = roi_get_height_1,
	.enum_active_sources = cm_enum_sources,
	.video_render = roi_render,
	.video_tick = roi_tick,
	.mouse_move = roi_mouse_move,
	.mouse_click = roi_mouse_click,
};
