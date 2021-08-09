#include <obs-module.h>
#include <QMainWindow>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QRadioButton>
#include <QDialogButtonBox>
#include "plugin-macros.generated.h"
#include "scope-dock.hpp"
#include "scope-dock-new-dialog.hpp"
#include "scope-widget.hpp"

ScopeDockNewDialog::ScopeDockNewDialog(QMainWindow *parent)
	: QDialog(parent)
{
	QLabel *label;
	int ix = 0;
	mainLayout = new QGridLayout;

	label = new QLabel(obs_module_text("Dock Title"));
	editTitle = new QLineEdit();
	mainLayout->addWidget(label, ix, 0, Qt::AlignRight);
	mainLayout->addWidget(editTitle, ix++, 1, Qt::AlignCenter);

	label = new QLabel(obs_module_text("Source"));
	radioProgram = new QRadioButton(obs_module_text("Program"));
	radioProgram->setChecked(true);
	radioPreview = new QRadioButton(obs_module_text("Preview"));
	mainLayout->addWidget(label, ix, 0, 3, 1, Qt::AlignRight);
	mainLayout->addWidget(radioProgram, ix++, 1, Qt::AlignLeft);
	mainLayout->addWidget(radioPreview, ix++, 1, Qt::AlignLeft);
	mainLayout->addWidget(new QLabel(obs_module_text("Other sources can be selected after creation")), ix++, 1, Qt::AlignLeft);
	// TODO: other sources

	QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	mainLayout->addWidget(buttonBox, ix++, 1, Qt::AlignRight);
	connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));

	setLayout(mainLayout);
}

ScopeDockNewDialog::~ScopeDockNewDialog()
{
}

void ScopeDockNewDialog::accept()
{
	const char *name = editTitle->text().toStdString().c_str();
	const char *srcName = NULL;
	obs_data_t *props = obs_data_create();
	obs_data_t *roi_prop = obs_data_create();
	if (radioPreview->isChecked())
		srcName = "\x10"; // preview

	if (srcName)
		obs_data_set_string(roi_prop, "target_name", srcName);
	obs_data_set_obj(props, "colormonitor_roi-prop", roi_prop);
	ScopeWidget::default_properties(props);

	scope_dock_add(name, props);

	obs_data_release(roi_prop);
	obs_data_release(props);

	QDialog::accept();
}
