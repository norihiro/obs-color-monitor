#include <obs-module.h>
#include <obs.h>
#include <obs-frontend-api.h>
#include <util/threading.h>
#include <string>
#include <algorithm>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include "plugin-macros.generated.h"
#include "scope-widget.hpp"
#include "scope-widget-properties.hpp"
#include "obsgui-helper.hpp"

#define N_SRC SCOPE_WIDGET_N_SRC

static const char *id_list[N_SRC] = {
	"colormonitor_roi",
	"vectorscope_source",
	"waveform_source",
	"histogram_source",
};

struct src_rect_s
{
	int x0, y0, x1, y1;
	int w, h;

	inline bool is_inside(int x, int y) {
		return x0<=x && x<=x1 && x0<x1 && y0<=y && y<=y1 && y0<y1;
	}
	inline int x_from_widget(int x) {
		return x1>x0 ? (x-x0) * w / (x1-x0) : 0;
	}
	inline int y_from_widget(int y) {
		return y1>y0 ? (y-y0) * h / (y1-y0) : 0;
	}
};

struct scope_widget_s
{
	obs_display_t *disp;
	obs_source_t *src[N_SRC];
	volatile uint32_t src_shown;
	pthread_mutex_t mutex;

	// last drawn coordinates for each rect
	src_rect_s src_rect[N_SRC];
};

static obs_source_t *create_scope_source_roi(const char *id, obs_data_t *settings, const char *name)
{
	const char *v_id = obs_get_latest_input_type_id(id);
	obs_source_t *src = obs_source_create(v_id, name, settings, NULL);

	return src;
}

static obs_source_t *create_scope_source(const char *id, obs_data_t *settings)
{
	std::string name;
	name = "dock-";
	name += id;

	const char *v_id = obs_get_latest_input_type_id(id);
	obs_source_t *src = obs_source_create_private(v_id, name.c_str(), settings);

	return src;
}

static void draw(void *param, uint32_t cx, uint32_t cy)
{
	auto *data = (struct scope_widget_s*)param;

	if (pthread_mutex_trylock(&data->mutex))
		return;

	int n_src = 0;
	const auto src_shown = data->src_shown;
	for (int i=0; i<N_SRC; i++) if (src_shown & (1<<i)) {
		n_src += 1;
	}

	int y0 = 0;
	for (int i=0, k=0; i<N_SRC; i++) if (data->src[i] && (src_shown & (1<<i))) {
		obs_source_t *s = data->src[i];
		int w_src = obs_source_get_width(s);
		int h_src = obs_source_get_height(s);
		int w = cx;
		int h = (cy-y0) / (n_src-k);
		switch (k) {
			case 0: // ROI
				if (w * h_src > h * w_src)
					w = h * w_src / h_src;
				else if (h * w_src > w * h_src)
					h = w * h_src / w_src;
				break;
			case 1: // vectorscope
				w = h = std::min(w, h);
				break;
		}

		gs_projection_push();
		gs_viewport_push();
		gs_set_viewport((cx-w)/2, y0, w, h);
		gs_ortho(0.0f, w_src, 0.0f, h_src, -100.0f, 100.0f);

		auto &r = data->src_rect[k];
		r.x0 = (cx-w)/2;
		r.y0 = y0;
		r.x1 = r.x0 + w;
		r.y1 = r.y0 + h;
		r.w = w_src;
		r.h = h_src;

		obs_source_video_render(s);

		gs_viewport_pop();
		gs_projection_pop();

		y0 += h;
		k ++;
	}

	pthread_mutex_unlock(&data->mutex);
}

ScopeWidget::ScopeWidget(QWidget *parent)
	: QWidget(parent)
{
	properties = NULL;
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);

	data = (struct scope_widget_s*)bzalloc(sizeof(struct scope_widget_s));
	pthread_mutex_init(&data->mutex, NULL);
	data->src_shown = (1<<N_SRC)-1;
}

ScopeWidget::~ScopeWidget()
{
	if (data) {
		obs_display_destroy(data->disp);
		data->disp = NULL;

		pthread_mutex_lock(&data->mutex);
		for (int i=0; i<N_SRC; i++) if (data->src[i]) {
			obs_source_release(data->src[i]);
			data->src[i] = NULL;
		}
		pthread_mutex_unlock(&data->mutex);

		pthread_mutex_destroy(&data->mutex);
	}
	bfree(data); data = NULL;
}

void ScopeWidget::CreateDisplay()
{
	if (data->disp)
		return;

	blog(LOG_INFO, "ScopeWidget::CreateDisplay %p", this);

	QSize size = GetPixelSize(this);
	gs_init_data info = {};
	info.cx = size.width();
	info.cy = size.height();
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	QWindow *window = windowHandle();
	if (!window) {
		blog(LOG_ERROR, "ScopeWidget %p: windowHandle() returns NULL", this);
		return;
	}
	if (!QTToGSWindow(window, info.window)) {
		blog(LOG_ERROR, "ScopeWidget %p: QTToGSWindow failed", this);
		return;
	}
	data->disp = obs_display_create(&info, 0);
	obs_display_add_draw_callback(data->disp, draw, data);
}

void ScopeWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	CreateDisplay();

	QSize size = GetPixelSize(this);
	obs_display_resize(data->disp, size.width(), size.height());
}

void ScopeWidget::paintEvent(QPaintEvent *event)
{
	CreateDisplay();
}

void ScopeWidget::closeEvent(QCloseEvent *event)
{
	setShown(false);
}

void ScopeWidget::setShown(bool shown)
{
	if (shown && !data->disp) {
		CreateDisplay();
	}
	if (!shown && data->disp) {
		obs_display_destroy(data->disp);
		data->disp = NULL;
	}
}

static void send_mouse_event(struct scope_widget_s *data, QMouseEvent *qt_event, uint32_t type, bool click, bool mouse_up)
{
	struct obs_mouse_event event = {};
	int x = qt_event->x();
	int y = qt_event->y();

	for (int i=0; i<N_SRC; i++) {
		auto &r = data->src_rect[i];
		if (r.is_inside(x, y)) {
			event.x = r.x_from_widget(x);
			event.y = r.y_from_widget(y);

			if (click)
				obs_source_send_mouse_click(data->src[i], &event, type, mouse_up, 1);
			// TODO: Not sending mouse_move because not used.
		}
	}
}

void ScopeWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		send_mouse_event(data, event, MOUSE_LEFT, true, false);
	}

	if (event->button() == Qt::RightButton) {
		QMenu popup(this);
		QAction *act;

		const char *menu_text[] = {
			"Show &ROI",
			"Show &Vectorscope",
			"Show &Waveform",
			"Show &Histogram",
		};

		for (int i=0; i<N_SRC; i++) {
			uint32_t mask = 1<<i;
			QAction *act = new QAction(obs_module_text(menu_text[i]), this);
			act->setCheckable(true);
			act->setChecked(!!(data->src_shown & mask));
			auto toggleCB = [=](bool checked) {
				if (checked)
					data->src_shown |= mask;
				else
					data->src_shown &= ~mask;
			};
			connect(act, &QAction::toggled, toggleCB);
			popup.addAction(act);
		}

		act = new QAction(obs_module_text("Properties..."), this);
		connect(act, &QAction::triggered, this, &ScopeWidget::createProperties);
		popup.addAction(act);

		popup.exec(QCursor::pos());
		return;
	}

	QWidget::mousePressEvent(event);
}

void ScopeWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		send_mouse_event(data, event, MOUSE_LEFT, true, true);
	}

	QWidget::mouseReleaseEvent(event);
}

void ScopeWidget::createProperties()
{
	if (properties) {
		if (!properties->close())
			return;
	}
	properties = new ScopeWidgetProperties(this, data->src);
	properties->Init();
	properties->setAttribute(Qt::WA_DeleteOnClose, true);
}

void ScopeWidget::default_properties(obs_data_t *props)
{
	for (int i=0; i<N_SRC; i++) {
		char s[32]; snprintf(s, sizeof(s), "%s-shown", id_list[i]); s[sizeof(s)-1]=0;
		obs_data_set_default_bool(props, s, true);
	}
}

void ScopeWidget::save_properties(obs_data_t *props)
{
	pthread_mutex_lock(&data->mutex);
	const auto src_shown = data->src_shown;
	for (int i=0; i<N_SRC; i++) {
		char s[32];
		snprintf(s, sizeof(s), "%s-shown", id_list[i]); s[sizeof(s)-1]=0;
		obs_data_set_bool(props, s, !!(src_shown & (1<<i)));

		if (data->src[i]) {
			snprintf(s, sizeof(s), "%s-prop", id_list[i]); s[sizeof(s)-1]=0;
			obs_data_t *prop = obs_source_get_settings(data->src[i]);
			if (prop) {
				obs_data_set_obj(props, s, prop);
				obs_data_release(prop);
			}
		}
	}
	pthread_mutex_unlock(&data->mutex);
}

void ScopeWidget::load_properties(obs_data_t *props)
{
	char roi_name[64]; sprintf(roi_name, "dock-roi-%p", this);

	pthread_mutex_lock(&data->mutex);
	data->src_shown = 0;
	for (int i=0; i<N_SRC; i++) {
		char s[32];
		snprintf(s, sizeof(s), "%s-shown", id_list[i]); s[sizeof(s)-1]=0;
		if (obs_data_get_bool(props, s))
			data->src_shown |= 1<<i;

		snprintf(s, sizeof(s), "%s-prop", id_list[i]); s[sizeof(s)-1]=0;
		obs_data_t *prop = obs_data_get_obj(props, s);
		if (!prop)
			prop = obs_data_create();

		if (i>0)
			obs_data_set_string(prop, "target_name", roi_name);

		if (!data->src[i])
			data->src[i] = i==0 ?
				create_scope_source_roi(id_list[i], prop, roi_name) :
				create_scope_source(id_list[i], prop);
		else
			obs_source_update(data->src[i], prop);

		obs_data_release(prop);
	}
	pthread_mutex_unlock(&data->mutex);
}
