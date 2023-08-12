#pragma once

#include <QDockWidget>
#include <QPointer>
#include <QAction>
#include <string>

class ScopeDock : public QDockWidget {
	Q_OBJECT

public:
	class ScopeWidget *widget;
	std::string name;
	QPointer<QAction> action = 0;

public:
	ScopeDock(QWidget *parent = nullptr);
	~ScopeDock();
	void closeEvent(QCloseEvent *event) override;

	void SetWidget(class ScopeWidget *w)
	{
		widget = w;
		setWidget((QWidget *)w);
	}

private:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
};

extern "C" void scope_dock_add(const char *name, obs_data_t *props);
extern "C" void scope_docks_init();
