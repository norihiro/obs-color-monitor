#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QAction>
#include <QMainWindow>
#include "plugin-macros.generated.h"
#include "scope-dock.hpp"
#include "scope-widget.hpp"

#define SAVE_DATA_NAME PLUGIN_NAME"-dock"
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
	w->load_properties(props);

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

void ScopeDock::showEvent(QShowEvent *event)
{
	blog(LOG_INFO, "ScopeDock::showEvent");
	widget->setShown(true);
}

void ScopeDock::hideEvent(QHideEvent *event)
{
	blog(LOG_INFO, "ScopeDock::hideEvent");
	widget->setShown(false);
}

static void save_load_scope_docks(obs_data_t *save_data, bool saving, void *)
{
	blog(LOG_INFO, "save_load_scope_docks");
	if (!docks)
		return;
	if (saving) {
		obs_data_t *props = obs_data_create();
		obs_data_array_t *array = obs_data_array_create();
		for (size_t i=0; i<docks->size(); i++) {
			ScopeDock *d = (*docks)[i];
			obs_data_t *obj = obs_data_create();
			d->widget->save_properties(obj);
			obs_data_set_string(obj, "name", d->name.c_str());
			obs_data_array_push_back(array, obj);
			obs_data_release(obj);
		}
		obs_data_set_array(props, "docks", array);
		obs_data_set_obj(save_data, SAVE_DATA_NAME, props);
		obs_data_array_release(array);
		obs_data_release(props);
	}

	else /* loading */ {
		obs_data_t *props = obs_data_get_obj(save_data, SAVE_DATA_NAME);
		if (!props) {
			props = obs_data_create();
			obs_data_array_t *array = obs_data_array_create();
			obs_data_t *obj = obs_data_create();
			ScopeWidget::default_properties(obj);
			obs_data_set_default_string(obj, "name", "program");
			obs_data_array_push_back(array, obj);
			obs_data_set_array(props, "docks", array);
			obs_data_array_release(array);
		}

		obs_data_array_t *array = obs_data_get_array(props, "docks");
		size_t count = obs_data_array_count(array);
		for (size_t i=0; i<count; i++) {
			obs_data_t *obj = obs_data_array_item(array, i);
			const char *name = obs_data_get_string(obj, "name");
			scope_dock_add(name, obj);
			obs_data_release(obj);
		}
		obs_data_array_release(array);
		obs_data_release(props);
	}
}

void scope_docks_init()
{
	docks = new std::vector<ScopeDock*>;
	obs_frontend_add_save_callback(save_load_scope_docks, NULL);
}

void scope_docks_release()
{
	delete docks;
	docks = NULL;
}
