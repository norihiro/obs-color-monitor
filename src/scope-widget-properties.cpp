#include <obs-module.h>
#include <obs.h>
#include <obs-frontend-api.h>
#include <QPushButton>
#include <QLayout>
#include <QCloseEvent>
#include <QMessageBox>
#include "plugin-macros.generated.h"
#include "scope-widget-properties.hpp"
#include "properties-view.hpp"

static obs_properties_t *scopewidget_properties(const obs_source_t *source)
{
	obs_properties_t *props = obs_source_properties(source);
	if (!props)
		return NULL;
	obs_property_set_visible(obs_properties_get(props, "target_name"), false);
	obs_property_set_visible(obs_properties_get(props, "target_scale"), false);
	obs_property_set_visible(obs_properties_get(props, "bypass"), false);
	return props;
}

ScopeWidgetProperties::ScopeWidgetProperties(QWidget *parent, obs_source_t *source_[]) : QDialog(parent)
{
	acceptClicked = false;
	for (int i = 0; i < SCOPE_WIDGET_N_SRC; i++) {
		source[i] = source_[i];
		// TODO: connect(obs_source_get_signal_handler(source[i]), "remove", ScopeWidgetProperties::SourceRemoved, this);
	}

	buttonBox = new QDialogButtonBox(this);
	buttonBox->setObjectName(QStringLiteral("buttonBox"));
	buttonBox->setStandardButtons(QDialogButtonBox::Ok);
	// TODO: QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);

	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	setMinimumWidth(360);
	setMinimumHeight(360);

	QMetaObject::connectSlotsByName(this);

	tabWidget = new QTabWidget(this);

	for (int i = 0; i < SCOPE_WIDGET_N_SRC; i++) {
		if (!source[i])
			continue;
		OBSData settings = obs_source_get_settings(source[i]);
		obs_data_release(settings);

		PropertiesReloadCallback prop_cb = (PropertiesReloadCallback)scopewidget_properties;
		if (i == 0)
			prop_cb = (PropertiesReloadCallback)obs_source_properties;

		PropertiesUpdateCallback handle_memory = [](void *vp, obs_data_t *new_settings) {
			obs_source_t *source = static_cast<obs_source_t *>(vp);

			obs_source_update(source, new_settings);
		};

		view[i] = new OBSPropertiesView(settings, source[i], prop_cb, handle_memory);
		const char *name = obs_source_get_display_name(obs_source_get_id(source[i]));
		tabWidget->addTab(view[i], name);
	}

	setLayout(new QVBoxLayout(this));
	layout()->addWidget(tabWidget);
	layout()->addWidget(buttonBox);
}

ScopeWidgetProperties::~ScopeWidgetProperties()
{
	static_cast<ScopeWidget *>(parent())->properties = NULL;
	// TODO: main->SaveProject();
}

void ScopeWidgetProperties::setTabIndex(int ix)
{
	blog(LOG_INFO, "ScopeWidgetProperties::setTabIndex(%d)", ix);
	if (tabWidget && 0 <= ix && ix < tabWidget->count())
		tabWidget->setCurrentIndex(ix);
}

void ScopeWidgetProperties::Init()
{
	show();
}

void ScopeWidgetProperties::Cleanup() {}

void ScopeWidgetProperties::closeEvent(QCloseEvent *event)
{
	QDialog::closeEvent(event);
	if (!event->isAccepted())
		return;

	Cleanup();
}

void ScopeWidgetProperties::on_buttonBox_clicked(QAbstractButton *button)
{
	QDialogButtonBox::ButtonRole val = buttonBox->buttonRole(button);
	if (val == QDialogButtonBox::AcceptRole) {
		acceptClicked = true;
		close();
	} else if (val == QDialogButtonBox::RejectRole) {
		// TODO: clear data
		static_cast<ScopeWidget *>(parent())->load_properties(oldSettings);
		close();
	} else if (val == QDialogButtonBox::ResetRole) {
		// TODO: implement me
	}
}
