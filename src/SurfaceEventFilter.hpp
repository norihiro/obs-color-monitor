#pragma once
#include <QObject>
#include <QPlatformSurfaceEvent>
#include "scope-widget.hpp"

class SurfaceEventFilter : public QObject {
	Q_OBJECT
	ScopeWidget *w;

public:
	SurfaceEventFilter(ScopeWidget *w_) : QObject(static_cast<QWidget *>(w_)), w(w_) {}

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;
};
