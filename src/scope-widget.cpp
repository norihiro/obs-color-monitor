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
	"vectorscope_source",
	"waveform_source",
	"histogram_source",
};

struct scope_widget_s
{
	obs_display_t *disp;
	obs_source_t *src[N_SRC];
	volatile uint32_t src_shown;
	pthread_mutex_t mutex;
};

static obs_source_t *create_scope_source(const char *id)
{
	std::string name;
	name = "dock-";
	name += id;

	const char *v_id = obs_get_latest_input_type_id(id);
	obs_source_t *src = obs_source_create_private(v_id, name.c_str(), NULL);

	return src;
}

static void draw(void *param, uint32_t cx, uint32_t cy)
{
	auto *data = (struct scope_widget_s*)param;

	if (pthread_mutex_trylock(&data->mutex))
		return;

	for (int i=0; i<N_SRC; i++) if (!data->src[i]) {
		data->src[i] = create_scope_source(id_list[i]);
		if (data->src[i]) {
			pthread_mutex_unlock(&data->mutex);
			return; // not to take too much time
		}
	}

	int n_src = 0;
	const auto src_shown = data->src_shown;
	for (int i=0; i<N_SRC; i++) if (src_shown & (1<<i)) {
		n_src += 1;
	}

	int y0 = 0;
	for (int i=0, k=0; i<N_SRC; i++) if (data->src[i] && (src_shown & (1<<i))) {
		obs_source_t *s = data->src[i];
		int w = cx;
		int h = (cy-y0) / (n_src-k);
		if (k==0)
			w = h = std::min(w, h);

		gs_projection_push();
		gs_viewport_push();
		gs_set_viewport((cx-w)/2, y0, w, h);
		gs_ortho(0.0f, obs_source_get_width(s), 0.0f, obs_source_get_height(s), -100.0f, 100.0f);

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

void ScopeWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::RightButton) {
		QMenu popup(this);
		QAction *act;

		const char *menu_text[] = {
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
			obs_data_t *prop = obs_data_create();
			if (prop) {
				obs_source_update(data->src[i], prop);
				obs_data_set_obj(props, s, prop);
				obs_data_release(prop);
			}
		}
	}
	pthread_mutex_unlock(&data->mutex);
}

void ScopeWidget::load_properties(obs_data_t *props)
{
	pthread_mutex_lock(&data->mutex);
	data->src_shown = 0;
	for (int i=0; i<N_SRC; i++) {
		char s[32];
		snprintf(s, sizeof(s), "%s-shown", id_list[i]); s[sizeof(s)-1]=0;
		if (obs_data_get_bool(props, s))
			data->src_shown |= 1<<i;

		if (!data->src[i])
			data->src[i] = create_scope_source(id_list[i]);
		snprintf(s, sizeof(s), "%s-prop", id_list[i]); s[sizeof(s)-1]=0;
		obs_data_t *prop = obs_data_get_obj(props, s);
		if (prop) {
			obs_source_update(data->src[i], prop);
			obs_data_release(prop);
		}
	}
	pthread_mutex_unlock(&data->mutex);
}