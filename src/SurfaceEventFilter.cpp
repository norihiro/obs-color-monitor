#include <obs-module.h>
#include "plugin-macros.generated.h"
#include "SurfaceEventFilter.hpp"

bool SurfaceEventFilter::eventFilter(QObject *obj, QEvent *event)
{
	bool result = QObject::eventFilter(obj, event);
	QPlatformSurfaceEvent *surfaceEvent;

	switch (event->type()) {
	case QEvent::PlatformSurface:
		surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);

		switch (surfaceEvent->surfaceEventType()) {
		case QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed:
			w->DestroyDisplay();
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return result;
}
