#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QAction>
#include <QMainWindow>
#include "plugin-macros.generated.h"
#include "scope-dock.hpp"
#include "scope-dock-new-dialog.hpp"
#include "scope-widget.hpp"

#define SAVE_DATA_NAME PLUGIN_NAME "-dock"
#define OBJ_NAME_SUFFIX "_scope_dock"

#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(30, 0, 0)
void ScopeDock::closeEvent(QCloseEvent *event)
{
	QDockWidget::closeEvent(event);
}
#endif

#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(30, 0, 0)
static std::vector<ScopeDock *> *docks;
#else
static std::vector<ScopeWidget *> *docks;
#endif

static inline bool is_program_dock(obs_data_t *props)
{
	bool ret = true;

	obs_data_t *roi_prop = obs_data_get_obj(props, "colormonitor_roi-prop");
	const char *target_name = obs_data_get_string(roi_prop, "target_name");
	if (target_name && *target_name)
		ret = false; // not program
	obs_data_release(roi_prop);

	return ret;
}

void scope_dock_add(const char *name, obs_data_t *props, bool show)
{
	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(30, 0, 0)
	UNUSED_PARAMETER(show);
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

	auto flag = is_program_dock(props) ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea;

	main_window->addDockWidget(flag, dock);
	dock->action = (QAction *)obs_frontend_add_dock(dock);

	if (docks)
		docks->push_back(dock);
#else
	ScopeWidget *w = new ScopeWidget(main_window);
	w->name = name;
	w->load_properties(props);
	if (!obs_frontend_add_dock_by_id(name, name, w)) {
		return;
	}

	if (docks)
		docks->push_back(w);

	if (show) {
		QMetaObject::invokeMethod(
			w,
			[w]() {
				auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
				QList<QDockWidget *> dd = main_window->findChildren<QDockWidget *>();
				for (QDockWidget *d : dd) {
					if (d->widget() == w) {
						d->setVisible(true);
					}
				}
			},
			Qt::QueuedConnection);
	}

#endif
}

#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(30, 0, 0)
ScopeDock::ScopeDock(QWidget *parent) : QDockWidget(parent)
{
	setAttribute(Qt::WA_DeleteOnClose);
}

ScopeDock::~ScopeDock()
{
	if (action)
		delete action;
	if (docks)
		for (size_t i = 0; i < docks->size(); i++) {
			if ((*docks)[i] == this) {
				docks->erase(docks->begin() + i);
				break;
			}
		}
}

void ScopeDock::showEvent(QShowEvent *)
{
	blog(LOG_INFO, "ScopeDock::showEvent");
	widget->setShown(true);
}

void ScopeDock::hideEvent(QHideEvent *)
{
	blog(LOG_INFO, "ScopeDock::hideEvent");
	widget->setShown(false);
}
#endif

static void close_all_docks()
{
	if (docks && docks->size()) {
		blog(LOG_INFO, "Closing %d remaining scope docks...", (int)docks->size());
		while (docks->size()) {
			(*docks)[docks->size() - 1]->close();
			delete (*docks)[docks->size() - 1];
		}
		blog(LOG_INFO, "Closed all remaining scope docks.");
	}
}

static void save_load_scope_docks(obs_data_t *save_data, bool saving, void *)
{
	blog(LOG_INFO, "save_load_scope_docks saving=%d", (int)saving);
	if (!docks)
		return;
	if (saving) {
		obs_data_t *props = obs_data_create();
		obs_data_array_t *array = obs_data_array_create();
		for (size_t i = 0; i < docks->size(); i++) {
#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(30, 0, 0)
			ScopeDock *d = (*docks)[i];
			ScopeWidget *w = d->widget;
			const char *name = d->name.c_str();
#else
			ScopeWidget *w = (*docks)[i];
			const char *name = w->name.c_str();
#endif
			obs_data_t *obj = obs_data_create();
			w->save_properties(obj);
			obs_data_set_string(obj, "name", name);
			obs_data_array_push_back(array, obj);
			obs_data_release(obj);
		}
		obs_data_set_array(props, "docks", array);
		obs_data_set_obj(save_data, SAVE_DATA_NAME, props);
		obs_data_array_release(array);
		obs_data_release(props);
	}

	else /* loading */ {
		close_all_docks();

		obs_data_t *props = obs_data_get_obj(save_data, SAVE_DATA_NAME);
		if (!props) {
			blog(LOG_INFO, "save_load_scope_docks: creating default properties");
			props = obs_data_create();
		}

		obs_data_array_t *array = obs_data_get_array(props, "docks");
		size_t count = obs_data_array_count(array);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *obj = obs_data_array_item(array, i);
			ScopeWidget::default_properties(obj);
			const char *name = obs_data_get_string(obj, "name");
			if (!name)
				name = "Scope: program";
			scope_dock_add(name, obj, false);
			obs_data_release(obj);
		}
		obs_data_array_release(array);
		obs_data_release(props);
	}
}

static void scope_docks_release();

static void frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(28, 0, 0)
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
#endif
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
	case OBS_FRONTEND_EVENT_EXIT:
		close_all_docks();

		if (event == OBS_FRONTEND_EVENT_EXIT)
			scope_docks_release();
		break;
	default:
		break;
	}
}

void scope_docks_init()
{
#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(30, 0, 0)
	docks = new std::vector<ScopeDock *>;
#else
	docks = new std::vector<ScopeWidget *>;
#endif
	obs_frontend_add_save_callback(save_load_scope_docks, NULL);
	obs_frontend_add_event_callback(frontend_event, nullptr);

	QAction *action =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("New Scope Dock...")));
	auto cb = [] {
		obs_frontend_push_ui_translation(obs_module_get_string);
		auto *dialog = new ScopeDockNewDialog(static_cast<QMainWindow *>(obs_frontend_get_main_window()));
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

	obs_frontend_remove_save_callback(save_load_scope_docks, NULL);
	obs_frontend_remove_event_callback(frontend_event, nullptr);
}

#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(30, 0, 0)
void scope_dock_deleted(class ScopeWidget *widget)
{
	if (!docks)
		return;

	for (size_t i = 0; i < docks->size(); i++) {
		if ((*docks)[i] != widget)
			continue;

		docks->erase(docks->begin() + i);
		break;
	}
}
#endif
