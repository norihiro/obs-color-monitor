#pragma once

#include <obs.h>
#include <QWidget>
#include <memory>
#include <string>
#include <OBSQTDisplay.hpp>

#define SCOPE_WIDGET_N_SRC 7

class ScopeWidget : public OBSQTDisplay {
	Q_OBJECT

	struct scope_widget_s *data;
	class ScopeWidgetProperties *properties;

public:
	std::string name;

private:
	void closeEvent(QCloseEvent *event) override;
	void RegisterCallbackToDisplay();

	// for interactions
	bool HandleMouseClickEvent(QMouseEvent *event);
	bool HandleMouseMoveEvent(QMouseEvent *event);
	bool HandleMouseWheelEvent(QWheelEvent *event);
	bool HandleKeyEvent(QKeyEvent *event);
	bool openMenu(QMouseEvent *event);

public slots:
	void createProperties();
	void RemoveDock();

public:
	ScopeWidget(QWidget *parent);
	~ScopeWidget();
	static void default_properties(obs_data_t *);
	void save_properties(obs_data_t *);
	void load_properties(obs_data_t *);

	friend class ScopeWidgetProperties;
	friend class ScopeWidgetInteractiveEventFilter;
};
