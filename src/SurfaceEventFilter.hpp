#pragma once
#include <QObject>

template<typename QTDisplay_class> class SurfaceEventFilter : public QObject {
	QTDisplay_class *w;

public:
	SurfaceEventFilter(QTDisplay_class *w_) : w(w_) {}

protected:
	bool eventFilter(QObject *obj, QEvent *event) override
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
};
