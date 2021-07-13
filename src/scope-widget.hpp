#pragma once

#include <obs.h>
#include <QWidget>

#define SCOPE_WIDGET_N_SRC 4

class ScopeWidget : public QWidget {
	Q_OBJECT

	struct scope_widget_s *data;
	class ScopeWidgetProperties *properties;

	void CreateDisplay();
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

public slots:
	void createProperties();

public:
	ScopeWidget(QWidget *parent);
	~ScopeWidget();
	static void default_properties(obs_data_t*);
	void save_properties(obs_data_t*);
	void load_properties(obs_data_t*);
	void setShown(bool shown);

	friend class ScopeWidgetProperties;
};
