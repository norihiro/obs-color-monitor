#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QAction>
#include <QMainWindow>
#include "plugin-macros.generated.h"
#include "scope-dock.hpp"
#include "scope-dock-new-dialog.hpp"
#include "scope-widget.hpp"

#define SAVE_DATA_NAME PLUGIN_NAME"-dock"
#define OBJ_NAME_SUFFIX "_scope_dock"

void ScopeDock::closeEvent(QCloseEvent *event)
{
	QDockWidget::closeEvent(event);
}

static std::vector<ScopeDock*> *docks;

void scope_dock_add(const char *name, obs_data_t *props)
{
	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new ScopeDock(main_window);
	dock->name = name;
	dock->setObjectName(QString::fromUtf8(name) + OBJ_NAME_SUFFIX);
	dock->setWindowTitle(name);
	dock->resize(256, 256);
	dock->setMinimumSize(128, 128);
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);

	ScopeWidget *w = new ScopeWidget(dock);
	dock->SetWidget(w);
	w->load_properties(props);

	main_window->addDockWidget(Qt::RightDockWidgetArea, dock);
	dock->action = (QAction*)obs_frontend_add_dock(dock);

	if (docks)
		docks->push_back(dock);
}

ScopeDock::ScopeDock(QWidget *parent)
	: QDockWidget(parent)
{
	setAttribute(Qt::WA_DeleteOnClose);
}

ScopeDock::~ScopeDock()
{
	if (action)
		delete action;
	if (docks) for (size_t i=0; i<docks->size(); i++) {
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
	blog(LOG_INFO, "save_load_scope_docks saving=%d", (int)saving);
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
		if (docks) while (docks->size()) {
			(*docks)[docks->size()-1]->close();
			delete (*docks)[docks->size()-1];
		}

		obs_data_t *props = obs_data_get_obj(save_data, SAVE_DATA_NAME);
		if (!props) {
			blog(LOG_INFO, "save_load_scope_docks: creating default properties");
			props = obs_data_create();
		}

		obs_data_array_t *array = obs_data_get_array(props, "docks");
		size_t count = obs_data_array_count(array);
		for (size_t i=0; i<count; i++) {
			obs_data_t *obj = obs_data_array_item(array, i);
			ScopeWidget::default_properties(obj);
			const char *name = obs_data_get_string(obj, "name");
			if (!name) name = "Scope: program";
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

	QAction *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(
				obs_module_text("New Scope Dock...") ));
	auto cb = [] {
		obs_frontend_push_ui_translation(obs_module_get_string);
		auto *dialog = new ScopeDockNewDialog(static_cast<QMainWindow *>(
					obs_frontend_get_main_window() ));
		dialog->show();
		dialog->setAttribute(Qt::WA_DeleteOnClose, true);
		obs_frontend_pop_ui_translation();
	};
	QAction::connect(action, &QAction::triggered, cb);
}

void scope_docks_release()
{
	delete docks;
	docks = NULL;
}
