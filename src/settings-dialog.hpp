#pragma once

#include "irl-config.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

// Modal dialog for editing the plugin configuration.
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(const IrlConfig &config, QWidget *parent = nullptr);

	IrlConfig result() const;

private:
	static void populateScenes(QComboBox *combo);
	static void populateTextSources(QComboBox *combo);
	static void selectOrInsert(QComboBox *combo, const QString &value);

	QComboBox *typeCombo = nullptr;
	QLineEdit *urlEdit = nullptr;
	QLineEdit *publisherEdit = nullptr;
	QComboBox *normalSceneCombo = nullptr;
	QComboBox *offlineSceneCombo = nullptr;
	QComboBox *infoSourceCombo = nullptr;
	QComboBox *statsSourceCombo = nullptr;
	QSpinBox *warnRttSpin = nullptr;
	QSpinBox *maxRttSpin = nullptr;
	QSpinBox *thresholdSpin = nullptr;
	QSpinBox *intervalSpin = nullptr;
	QCheckBox *startPausedCheck = nullptr;
};
