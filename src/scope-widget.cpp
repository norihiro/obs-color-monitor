#include <obs-module.h>
#include <obs-frontend-api.h>
#include "plugin-macros.generated.h"
#include "scope-widget.hpp"
#include "obsgui-helper.hpp"

struct scope_widget_s
{
	obs_display_t *disp;
};

static void draw(void *param, uint32_t cx, uint32_t cy)
{
	auto *data = (struct scope_widget_s*)param;

	obs_render_main_texture(); // TODO: try this
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
}

ScopeWidget::~ScopeWidget()
{
	if (data) {
		obs_display_destroy(data->disp);
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
