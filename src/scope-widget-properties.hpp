#pragma once

#include <obs.h>
#include <QWidget>
#include <QDialog>
#include <QSplitter>
#include <QTabWidget>
#include <QAbstractButton>
#include <QDialogButtonBox>
#include <obs.hpp>
#include "scope-widget.hpp"

class ScopeWidgetProperties : public QDialog {
	Q_OBJECT

private:
	OBSSource source[SCOPE_WIDGET_N_SRC];
	OBSData oldSettings;

	OBSSignal removedSignal[SCOPE_WIDGET_N_SRC];
	OBSSignal renamedSignal[SCOPE_WIDGET_N_SRC];
	class OBSPropertiesView *view[SCOPE_WIDGET_N_SRC];
	class QTabWidget *tabWidget;
	class QDialogButtonBox *buttonBox;
	class QSplitter *splitter;
	bool acceptClicked;

private slots:
	void on_buttonBox_clicked(QAbstractButton *button);

public:
	ScopeWidgetProperties(QWidget *parent, obs_source_t *source_[]);
	~ScopeWidgetProperties();

	void setTabIndex(int);
	void Init();
	void Cleanup();

protected:
	void closeEvent(QCloseEvent *event) override;
};

