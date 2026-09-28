/*
 * obs-irl-control
 * Copyright (C) 2026 Anikeen UG (haftungsbeschränkt) & Co. KG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

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
