#pragma once

#include <QDockWidget>
#include <string>

class ScopeDock : public QDockWidget {
	Q_OBJECT

	class ScopeWidget *widget;
public:
	std::string name;

public:
	ScopeDock(QWidget *parent = nullptr);
	~ScopeDock();
	virtual void closeEvent(QCloseEvent *event);

	void SetWidget(class ScopeWidget *w) { widget = w; setWidget((QWidget*)w); }
};

extern "C" void scope_docks_init();
extern "C" void scope_docks_release();
