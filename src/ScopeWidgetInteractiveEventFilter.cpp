#include <obs-module.h>
#include <QKeyEvent>
#include "plugin-macros.generated.h"
#include "ScopeWidgetInteractiveEventFilter.hpp"

bool ScopeWidgetInteractiveEventFilter::eventFilter(QObject *, QEvent *event)
{
	switch (event->type()) {
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	case QEvent::MouseButtonDblClick:
		return parent->HandleMouseClickEvent(static_cast<QMouseEvent *>(event));

	case QEvent::MouseMove:
		return parent->HandleMouseMoveEvent(static_cast<QMouseEvent *>(event));

	case QEvent::Wheel:
		return parent->HandleMouseWheelEvent(static_cast<QWheelEvent *>(event));

	case QEvent::KeyPress:
	case QEvent::KeyRelease:
		return parent->HandleKeyEvent(static_cast<QKeyEvent *>(event));

	default:
		return false;
	}
}
