#pragma once

#include <QWidget>

class ScopeWidget : public QWidget {
	Q_OBJECT

	struct scope_widget_s *data;

	void CreateDisplay();
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void closeEvent(QCloseEvent *event) override;

public:
	ScopeWidget(QWidget *parent);
	~ScopeWidget();
	void setShown(bool shown);
};
