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

#include <QPointer>
#include <QWidget>

class SettingsDialog;

class QLabel;
class QPushButton;
class QTimer;

// Dock panel showing the live health-check state with manual controls.
class IrlDock : public QWidget {
	Q_OBJECT

public:
	explicit IrlDock(QWidget *parent = nullptr);

private Q_SLOTS:
	void refresh();
	void onTogglePause();
	void onForceOnline();
	void onForceOffline();
	void onOpenSettings();

private:
	QLabel *statusLabel = nullptr;
	QLabel *rttLabel = nullptr;
	QLabel *offlineLabel = nullptr;
	QLabel *statsLabel = nullptr;
	QLabel *errorLabel = nullptr;
	QPushButton *pauseButton = nullptr;
	QPushButton *onlineButton = nullptr;
	QPushButton *offlineButton = nullptr;
	QPushButton *settingsButton = nullptr;
	QTimer *timer = nullptr;
	QPointer<SettingsDialog> settingsDialog;
};
