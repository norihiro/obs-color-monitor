#pragma once

#include <QDockWidget>
#include <string>

class ScopeDock : public QDockWidget {
	Q_OBJECT

public:
	std::string name;

public:
	ScopeDock(QWidget *parent = nullptr);
	~ScopeDock();
	virtual void closeEvent(QCloseEvent *event);
};

extern "C" void scope_docks_init();
extern "C" void scope_docks_release();
