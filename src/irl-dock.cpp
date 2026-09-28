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

#include "irl-dock.hpp"
#include "irl-controller.hpp"
#include "settings-dialog.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString tr_(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

constexpr const char *COLOR_GREEN = "#3fb950";
constexpr const char *COLOR_ORANGE = "#d29922";
constexpr const char *COLOR_RED = "#f85149";
constexpr const char *COLOR_GRAY = "#8b949e";

QString colored(const QString &text, const char *color)
{
	return QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(QString::fromUtf8(color), text.toHtmlEscaped());
}

} // namespace

IrlDock::IrlDock(QWidget *parent) : QWidget(parent)
{
	// OBS themes style the dock body via "OBSDock > QWidget" (background, border, radius).
	// Qt only paints stylesheet backgrounds on QWidget subclasses that opt in.
	setAttribute(Qt::WA_StyledBackground, true);

	// Mirror the native docks: a zero-margin content widget holding a padded frame.
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	auto *frame = new QFrame(this);
	frame->setObjectName("irlControlFrame");
	frame->setFrameShape(QFrame::NoFrame);
	outer->addWidget(frame);

	auto *layout = new QVBoxLayout(frame);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(4);

	statusLabel = new QLabel(this);
	statusLabel->setTextFormat(Qt::RichText);
	statusLabel->setWordWrap(true);
	statusLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
	layout->addWidget(statusLabel);

	auto *form = new QFormLayout();
	form->setContentsMargins(0, 0, 0, 0);
	rttLabel = new QLabel(this);
	rttLabel->setTextFormat(Qt::RichText);
	form->addRow(tr_("IrlControl.Label.Rtt") + ":", rttLabel);
	offlineLabel = new QLabel(this);
	form->addRow(tr_("IrlControl.Label.OfflineFor") + ":", offlineLabel);
	layout->addLayout(form);

	statsLabel = new QLabel(this);
	statsLabel->setWordWrap(true);
	statsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	statsLabel->setStyleSheet("font-family: monospace; font-size: 11px;");
	layout->addWidget(statsLabel);

	errorLabel = new QLabel(this);
	errorLabel->setWordWrap(true);
	errorLabel->setTextFormat(Qt::RichText);
	errorLabel->setVisible(false);
	layout->addWidget(errorLabel);

	layout->addSpacing(4);

	auto *buttons = new QHBoxLayout();
	pauseButton = new QPushButton(tr_("IrlControl.Button.Pause"), this);
	onlineButton = new QPushButton(tr_("IrlControl.Button.ForceOnline"), this);
	offlineButton = new QPushButton(tr_("IrlControl.Button.ForceOffline"), this);
	buttons->addWidget(pauseButton);
	buttons->addWidget(onlineButton);
	buttons->addWidget(offlineButton);
	layout->addLayout(buttons);

	settingsButton = new QPushButton(tr_("IrlControl.Button.Settings"), this);
	layout->addWidget(settingsButton);
	layout->addStretch(1);

	connect(pauseButton, &QPushButton::clicked, this, &IrlDock::onTogglePause);
	connect(onlineButton, &QPushButton::clicked, this, &IrlDock::onForceOnline);
	connect(offlineButton, &QPushButton::clicked, this, &IrlDock::onForceOffline);
	connect(settingsButton, &QPushButton::clicked, this, &IrlDock::onOpenSettings);

	timer = new QTimer(this);
	timer->setInterval(500);
	connect(timer, &QTimer::timeout, this, &IrlDock::refresh);
	timer->start();

	refresh();
}

void IrlDock::refresh()
{
	IrlController &ctl = IrlController::instance();
	const IrlConfig cfg = ctl.config();
	const HealthSnapshot s = ctl.snapshot();

	if (!ctl.isConfigured()) {
		statusLabel->setText(colored(tr_("IrlControl.Status.NotConfigured"), COLOR_GRAY));
		rttLabel->setText("-");
		offlineLabel->setText("-");
		statsLabel->clear();
		errorLabel->setVisible(false);
		pauseButton->setEnabled(false);
		pauseButton->setText(tr_("IrlControl.Button.Pause"));
		return;
	}

	pauseButton->setEnabled(s.running);
	pauseButton->setText(s.paused ? tr_("IrlControl.Button.Resume") : tr_("IrlControl.Button.Pause"));

	QString status;
	const char *statusColor = COLOR_GRAY;
	if (!s.running) {
		status = tr_("IrlControl.Status.Stopped");
	} else if (s.paused) {
		status = tr_("IrlControl.Status.Paused");
		statusColor = COLOR_ORANGE;
	} else if (s.state == StreamState::Online) {
		status = tr_("IrlControl.Status.Online");
		statusColor = COLOR_GREEN;
	} else if (s.state == StreamState::Offline) {
		status = tr_("IrlControl.Status.Offline").replace("%1", QString::number(s.offlineDuration));
		statusColor = s.markedOffline ? COLOR_RED : COLOR_ORANGE;
		if (s.markedOffline)
			status += " - " + tr_("IrlControl.Status.SwitchedOffline");
	} else {
		status = tr_("IrlControl.Status.Waiting");
	}
	statusLabel->setText(colored(status, statusColor));

	if (s.state == StreamState::Unknown || s.msRtt <= 0.0) {
		rttLabel->setText("-");
	} else {
		const char *rttColor = COLOR_GREEN;
		if (s.msRtt > cfg.maxMsRtt)
			rttColor = COLOR_RED;
		else if (s.msRtt > cfg.warnMsRtt)
			rttColor = COLOR_ORANGE;
		rttLabel->setText(colored(QString::number(s.msRtt, 'f', 0) + " ms", rttColor));
	}

	offlineLabel->setText(s.state == StreamState::Offline ? QString::number(s.offlineDuration) + " s" : "0 s");
	statsLabel->setText(QString::fromStdString(s.statsText));

	if (!s.lastError.empty()) {
		errorLabel->setText(colored(tr_("IrlControl.Label.LastError") + ": " + QString::fromStdString(s.lastError),
					    COLOR_RED));
		errorLabel->setVisible(true);
	} else {
		errorLabel->setVisible(false);
	}
}

void IrlDock::onTogglePause()
{
	IrlController::instance().togglePause();
	refresh();
}

void IrlDock::onForceOnline()
{
	IrlController::instance().forceOnline();
}

void IrlDock::onForceOffline()
{
	IrlController::instance().forceOffline();
}

void IrlDock::onOpenSettings()
{
	if (settingsDialog) {
		settingsDialog->raise();
		settingsDialog->activateWindow();
		return;
	}

	IrlController &ctl = IrlController::instance();
	// Parent to the main window like OBS' own dialogs. The dialog is non-modal on purpose:
	// Wayland compositors pin modal dialogs to their parent and drag the parent with them.
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	settingsDialog = new SettingsDialog(ctl.config(), mainWindow);
	settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
	connect(settingsDialog, &QDialog::accepted, this, [this]() {
		if (settingsDialog)
			IrlController::instance().applyConfig(settingsDialog->result());
		refresh();
	});
	settingsDialog->show();
}
