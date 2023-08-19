#pragma once
#include <QObject>
#include <QEvent>
#include "scope-widget.hpp"

class ScopeWidgetInteractiveEventFilter : public QObject {
	Q_OBJECT
	ScopeWidget *parent;

public:
	ScopeWidgetInteractiveEventFilter(ScopeWidget *p) : QObject(p), parent(p) {}

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;
};
