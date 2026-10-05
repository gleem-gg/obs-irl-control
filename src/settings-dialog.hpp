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

#include <string>
#include <vector>

struct GleemDevice;

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
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
	void updateServerFields();
	// Gleem: fetches the account's IRL Sidekicks in the background and fills deviceCombo.
	void loadDevices();
	void showDevices(unsigned request, bool ok, const std::vector<GleemDevice> &devices, const std::string &error);
	void setDeviceItems(const std::vector<GleemDevice> &devices);
	QString selectedDevice() const;

	QComboBox *typeCombo = nullptr;
	QLineEdit *urlEdit = nullptr;
	QLineEdit *publisherEdit = nullptr;
	QLineEdit *tokenEdit = nullptr;
	QWidget *deviceRow = nullptr;
	QComboBox *deviceCombo = nullptr;
	QPushButton *refreshButton = nullptr;
	QLabel *deviceHint = nullptr;
	// The device to keep selected while the list (re)loads.
	QString wantedDevice;
	// Only the newest request may fill the list.
	unsigned deviceRequest = 0;
	QFormLayout *serverForm = nullptr;
	QLabel *urlHint = nullptr;
	QLabel *publisherHint = nullptr;
	QLabel *tokenHint = nullptr;
	// Gleem: the environment provides token and API URL, so their fields are hidden.
	bool envCredentials = false;
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
