#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QAction>
#include <QMainWindow>
#include "plugin-macros.generated.h"
#include "scope-dock.hpp"
#include "scope-widget.hpp"

#define OBJ_NAME_SUFFIX "_scope_dock"

void ScopeDock::closeEvent(QCloseEvent *event)
{
	QDockWidget::closeEvent(event);
}

static std::vector<ScopeDock*> *docks;

static void scope_dock_add(const char *name, obs_data_t *props)
{
	auto *dock = new ScopeDock();
	dock->name = name;
	dock->setObjectName(QString(name) + OBJ_NAME_SUFFIX);
	dock->setWindowTitle(QString("Scope: ") + name);
	dock->resize(256, 256);
	dock->setMinimumSize(128, 128);
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);

	ScopeWidget *w = new ScopeWidget(dock);
	dock->SetWidget(w);

	auto *main = (QMainWindow*)obs_frontend_get_main_window();
	main->addDockWidget(Qt::RightDockWidgetArea, dock);
	QAction *action = (QAction*)obs_frontend_add_dock(dock);

	if (docks)
		docks->push_back(dock);
}

ScopeDock::ScopeDock(QWidget *parent)
	: QDockWidget(parent)
{
}

ScopeDock::~ScopeDock()
{
	if (docks) for (int i=0; i<docks->size(); i++) {
		if ((*docks)[i] == this) {
			docks->erase(docks->begin()+i);
			break;
		}
	}
}

void scope_docks_init()
{
	docks = new std::vector<ScopeDock*>;
}

void scope_docks_release()
{
	delete docks;
	docks = NULL;
}
