#pragma once

#include <obs.h>
#include <QWidget>
#include <memory>

#define SCOPE_WIDGET_N_SRC 6

class OBSEventFilter;

class ScopeWidget : public QWidget {
	Q_OBJECT

	struct scope_widget_s *data;
	class ScopeWidgetProperties *properties;
	std::unique_ptr<OBSEventFilter> eventFilter;

	void CreateDisplay();
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	class QPaintEngine *paintEngine() const override;
	void closeEvent(QCloseEvent *event) override;

	// for interactions
	bool HandleMouseClickEvent(QMouseEvent *event);
	bool HandleMouseMoveEvent(QMouseEvent *event);
	bool HandleMouseWheelEvent(QWheelEvent *event);
	bool HandleKeyEvent(QKeyEvent *event);
	bool openMenu(QMouseEvent *event);
	OBSEventFilter *BuildEventFilter();

public slots:
	void createProperties();

public:
	ScopeWidget(QWidget *parent);
	~ScopeWidget();
	static void default_properties(obs_data_t *);
	void save_properties(obs_data_t *);
	void load_properties(obs_data_t *);
	void setShown(bool shown);

	friend class ScopeWidgetProperties;
};

typedef std::function<bool(QObject *, QEvent *)> EventFilterFunc;

class OBSEventFilter : public QObject {
	Q_OBJECT
public:
	OBSEventFilter(EventFilterFunc filter_) : filter(filter_) {}

protected:
	bool eventFilter(QObject *obj, QEvent *event) { return filter(obj, event); }

public:
	EventFilterFunc filter;
};
