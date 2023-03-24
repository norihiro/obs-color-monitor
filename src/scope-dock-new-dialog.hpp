#pragma once

#include <obs.h>
#include <QDialog>

class ScopeDockNewDialog : public QDialog {
	Q_OBJECT
	class QGridLayout *mainLayout;
	class QLineEdit *editTitle;
	class QRadioButton *radioProgram;
	class QRadioButton *radioPreview;
	// class QRadioButton *radioSource;
	// class QComboBox *editSourceName;

public:
	ScopeDockNewDialog(class QMainWindow *parent = NULL);
	~ScopeDockNewDialog();

public slots:
	void accept() override;
};
