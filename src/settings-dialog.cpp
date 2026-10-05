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

#include "settings-dialog.hpp"
#include "stats-server.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cstring>
#include <thread>

namespace {

QString tr_(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

bool enum_text_sources(void *param, obs_source_t *source)
{
	const char *id = obs_source_get_unversioned_id(source);
	if (!id)
		return true;
	if (strcmp(id, "text_gdiplus") == 0 || strcmp(id, "text_ft2_source") == 0 ||
	    strcmp(id, "text_pango_source") == 0) {
		static_cast<QComboBox *>(param)->addItem(QString::fromUtf8(obs_source_get_name(source)));
	}
	return true;
}

} // namespace

SettingsDialog::SettingsDialog(const IrlConfig &config, QWidget *parent) : QDialog(parent, Qt::Dialog)
{
	setWindowTitle(tr_("IrlControl.Settings.Title"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setWindowModality(Qt::NonModal);
	setSizeGripEnabled(true);
	setMinimumWidth(520);
	resize(560, 640);

	auto *layout = new QVBoxLayout(this);

	// Stats server
	auto *serverBox = new QGroupBox(tr_("IrlControl.Settings.StatsServer"), this);
	serverForm = new QFormLayout(serverBox);

	typeCombo = new QComboBox(serverBox);
	typeCombo->addItem(tr_("IrlControl.Settings.Type.Gleem"),
			   QString::fromUtf8(IrlConfig::typeToString(StatsServerType::Gleem)));
	typeCombo->addItem(tr_("IrlControl.Settings.Type.BelaboxCloud"),
			   QString::fromUtf8(IrlConfig::typeToString(StatsServerType::BelaboxCloud)));
	typeCombo->addItem(tr_("IrlControl.Settings.Type.SrtRelay"),
			   QString::fromUtf8(IrlConfig::typeToString(StatsServerType::SrtRelay)));
	typeCombo->setCurrentIndex(typeCombo->findData(QString::fromUtf8(IrlConfig::typeToString(config.statsType))));
	serverForm->addRow(tr_("IrlControl.Settings.Type"), typeCombo);

	auto makeHint = [serverBox]() {
		auto *hint = new QLabel(serverBox);
		hint->setWordWrap(true);
		hint->setStyleSheet("color: gray; font-size: 11px;");
		return hint;
	};

	tokenEdit = new QLineEdit(QString::fromStdString(config.apiToken), serverBox);
	tokenEdit->setEchoMode(QLineEdit::Password);
	tokenEdit->setPlaceholderText("gleem_pat_...");
	serverForm->addRow(tr_("IrlControl.Settings.Token"), tokenEdit);
	tokenHint = makeHint();
	tokenHint->setText(tr_("IrlControl.Settings.Token.Hint"));
	if (!config.envToken.empty()) {
		// A rented Gleem OBS machine brings its own token. A token typed here
		// still wins, so the field stays editable.
		tokenEdit->setPlaceholderText(tr_("IrlControl.Settings.Token.FromRental"));
		tokenHint->setText(tr_("IrlControl.Settings.Token.FromRental.Hint"));
	}
	tokenHint->setTextFormat(Qt::RichText);
	tokenHint->setOpenExternalLinks(true);
	serverForm->addRow(QString(), tokenHint);

	urlEdit = new QLineEdit(QString::fromStdString(config.statsUrl), serverBox);
	serverForm->addRow(tr_("IrlControl.Settings.Url"), urlEdit);
	urlHint = makeHint();
	serverForm->addRow(QString(), urlHint);

	const bool gleemConfig = config.statsType == StatsServerType::Gleem;

	// srtrelay / Belabox: a typed publisher. Gleem: a device from the account.
	publisherEdit = new QLineEdit(gleemConfig ? QString() : QString::fromStdString(config.publisher), serverBox);
	serverForm->addRow(tr_("IrlControl.Settings.Publisher"), publisherEdit);
	publisherHint = makeHint();
	serverForm->addRow(QString(), publisherHint);

	deviceRow = new QWidget(serverBox);
	auto *deviceLayout = new QHBoxLayout(deviceRow);
	deviceLayout->setContentsMargins(0, 0, 0, 0);
	deviceCombo = new QComboBox(deviceRow);
	deviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	deviceLayout->addWidget(deviceCombo);
	refreshButton = new QPushButton(tr_("IrlControl.Settings.Device.Refresh"), deviceRow);
	deviceLayout->addWidget(refreshButton);
	serverForm->addRow(tr_("IrlControl.Settings.Publisher.Gleem"), deviceRow);
	deviceHint = makeHint();
	serverForm->addRow(QString(), deviceHint);

	wantedDevice = gleemConfig ? QString::fromStdString(config.publisher) : QString();
	setDeviceItems({});

	connect(deviceCombo, &QComboBox::activated, this, [this]() { wantedDevice = selectedDevice(); });
	connect(refreshButton, &QPushButton::clicked, this, [this]() { loadDevices(); });
	// Another token or API URL may see other devices.
	connect(tokenEdit, &QLineEdit::editingFinished, this, [this]() {
		if (tokenEdit->isModified()) {
			tokenEdit->setModified(false);
			loadDevices();
		}
	});
	connect(urlEdit, &QLineEdit::editingFinished, this, [this]() {
		if (urlEdit->isModified() && deviceRow->isVisibleTo(this)) {
			urlEdit->setModified(false);
			loadDevices();
		}
	});

	layout->addWidget(serverBox);

	// Scenes
	auto *sceneBox = new QGroupBox(tr_("IrlControl.Settings.Scenes"), this);
	auto *sceneForm = new QFormLayout(sceneBox);

	normalSceneCombo = new QComboBox(sceneBox);
	normalSceneCombo->setEditable(true);
	populateScenes(normalSceneCombo);
	selectOrInsert(normalSceneCombo, QString::fromStdString(config.normalScene));
	sceneForm->addRow(tr_("IrlControl.Settings.NormalScene"), normalSceneCombo);

	offlineSceneCombo = new QComboBox(sceneBox);
	offlineSceneCombo->setEditable(true);
	populateScenes(offlineSceneCombo);
	selectOrInsert(offlineSceneCombo, QString::fromStdString(config.offlineScene));
	sceneForm->addRow(tr_("IrlControl.Settings.OfflineScene"), offlineSceneCombo);

	layout->addWidget(sceneBox);

	// Text sources
	auto *sourceBox = new QGroupBox(tr_("IrlControl.Settings.Sources"), this);
	auto *sourceForm = new QFormLayout(sourceBox);

	infoSourceCombo = new QComboBox(sourceBox);
	infoSourceCombo->setEditable(true);
	infoSourceCombo->addItem(QString());
	populateTextSources(infoSourceCombo);
	selectOrInsert(infoSourceCombo, QString::fromStdString(config.infoSource));
	sourceForm->addRow(tr_("IrlControl.Settings.InfoSource"), infoSourceCombo);

	statsSourceCombo = new QComboBox(sourceBox);
	statsSourceCombo->setEditable(true);
	statsSourceCombo->addItem(QString());
	populateTextSources(statsSourceCombo);
	selectOrInsert(statsSourceCombo, QString::fromStdString(config.statsSource));
	sourceForm->addRow(tr_("IrlControl.Settings.StatsSource"), statsSourceCombo);

	layout->addWidget(sourceBox);

	// Health check
	auto *healthBox = new QGroupBox(tr_("IrlControl.Settings.HealthCheck"), this);
	auto *healthForm = new QFormLayout(healthBox);

	warnRttSpin = new QSpinBox(healthBox);
	warnRttSpin->setRange(0, 60000);
	warnRttSpin->setSuffix(" ms");
	warnRttSpin->setValue(config.warnMsRtt);
	healthForm->addRow(tr_("IrlControl.Settings.WarnRtt"), warnRttSpin);

	maxRttSpin = new QSpinBox(healthBox);
	maxRttSpin->setRange(0, 60000);
	maxRttSpin->setSuffix(" ms");
	maxRttSpin->setValue(config.maxMsRtt);
	healthForm->addRow(tr_("IrlControl.Settings.MaxRtt"), maxRttSpin);

	thresholdSpin = new QSpinBox(healthBox);
	thresholdSpin->setRange(0, 3600);
	thresholdSpin->setSuffix(" s");
	thresholdSpin->setValue(config.offlineThresholdSec);
	healthForm->addRow(tr_("IrlControl.Settings.OfflineThreshold"), thresholdSpin);

	intervalSpin = new QSpinBox(healthBox);
	intervalSpin->setRange(250, 60000);
	intervalSpin->setSingleStep(250);
	intervalSpin->setSuffix(" ms");
	intervalSpin->setValue(config.intervalMs);
	healthForm->addRow(tr_("IrlControl.Settings.Interval"), intervalSpin);

	startPausedCheck = new QCheckBox(tr_("IrlControl.Settings.StartPaused"), healthBox);
	startPausedCheck->setChecked(config.startPaused);
	healthForm->addRow(QString(), startPausedCheck);

	layout->addWidget(healthBox);

	connect(typeCombo, &QComboBox::currentIndexChanged, this, [this]() {
		updateServerFields();
		if (deviceRow->isVisibleTo(this))
			loadDevices();
	});
	updateServerFields();
	if (gleemConfig)
		loadDevices();

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

IrlConfig SettingsDialog::result() const
{
	IrlConfig c;
	c.statsType = IrlConfig::typeFromString(typeCombo->currentData().toString().toUtf8().constData());
	c.statsUrl = urlEdit->text().trimmed().toStdString();
	c.publisher = c.statsType == StatsServerType::Gleem ? selectedDevice().toStdString()
							    : publisherEdit->text().trimmed().toStdString();
	c.apiToken = tokenEdit->text().trimmed().toStdString();
	c.normalScene = normalSceneCombo->currentText().trimmed().toStdString();
	c.offlineScene = offlineSceneCombo->currentText().trimmed().toStdString();
	c.infoSource = infoSourceCombo->currentText().trimmed().toStdString();
	c.statsSource = statsSourceCombo->currentText().trimmed().toStdString();
	c.warnMsRtt = warnRttSpin->value();
	c.maxMsRtt = maxRttSpin->value();
	c.offlineThresholdSec = thresholdSpin->value();
	c.intervalMs = intervalSpin->value();
	c.startPaused = startPausedCheck->isChecked();
	return c;
}

void SettingsDialog::updateServerFields()
{
	const StatsServerType type = IrlConfig::typeFromString(typeCombo->currentData().toString().toUtf8().constData());
	const bool gleem = type == StatsServerType::Gleem;

	serverForm->setRowVisible(tokenEdit, gleem);
	serverForm->setRowVisible(tokenHint, gleem);
	serverForm->setRowVisible(publisherEdit, !gleem);
	serverForm->setRowVisible(publisherHint, !gleem);
	serverForm->setRowVisible(deviceRow, gleem);
	serverForm->setRowVisible(deviceHint, gleem);

	auto setLabel = [this](QWidget *field, const char *key) {
		if (auto *label = qobject_cast<QLabel *>(serverForm->labelForField(field)))
			label->setText(tr_(key));
	};

	switch (type) {
	case StatsServerType::Gleem:
		setLabel(urlEdit, "IrlControl.Settings.Url.Gleem");
		urlEdit->setPlaceholderText(QString::fromUtf8(GLEEM_DEFAULT_URL));
		urlHint->setText(tr_("IrlControl.Settings.Url.Gleem.Hint"));
		break;
	case StatsServerType::SrtRelay:
		setLabel(urlEdit, "IrlControl.Settings.Url");
		urlEdit->setPlaceholderText("http://127.0.0.1:34101");
		publisherEdit->setPlaceholderText("publish/test/");
		urlHint->setText(tr_("IrlControl.Settings.Url.Hint"));
		publisherHint->setText(tr_("IrlControl.Settings.Publisher.Hint"));
		break;
	default:
		setLabel(urlEdit, "IrlControl.Settings.Url");
		urlEdit->setPlaceholderText("https://stats.srt.belabox.net/XXXXX");
		publisherEdit->setPlaceholderText("live");
		urlHint->setText(tr_("IrlControl.Settings.Url.Hint"));
		publisherHint->setText(tr_("IrlControl.Settings.Publisher.Hint"));
		break;
	}

	intervalSpin->setMinimum(gleem ? GLEEM_MIN_INTERVAL_MS : 250);
}

void SettingsDialog::loadDevices()
{
	// A fresh IrlConfig carries the environment's token and URL as fallbacks.
	IrlConfig c;
	c.statsType = StatsServerType::Gleem;
	c.statsUrl = urlEdit->text().trimmed().toStdString();
	c.apiToken = tokenEdit->text().trimmed().toStdString();

	const unsigned request = ++deviceRequest;
	if (c.gleemToken().empty()) {
		setDeviceItems({});
		refreshButton->setEnabled(true);
		deviceHint->setText(tr_("IrlControl.Settings.Device.NoToken"));
		return;
	}

	refreshButton->setEnabled(false);
	deviceHint->setText(tr_("IrlControl.Settings.Device.Loading"));

	// The request blocks for up to a few seconds, so it runs off the UI thread.
	// The result is handed back through the event loop and dropped when the
	// dialog has been closed in the meantime.
	QPointer<SettingsDialog> self(this);
	std::thread([self = std::move(self), request, c = std::move(c)]() mutable {
		std::vector<GleemDevice> devices;
		std::string error;
		const bool ok = StatsServer(c).listGleemDevices(devices, error);
		QMetaObject::invokeMethod(
			qApp,
			[self = std::move(self), request, ok, devices = std::move(devices), error = std::move(error)]() {
				if (self)
					self->showDevices(request, ok, devices, error);
			},
			Qt::QueuedConnection);
	}).detach();
}

void SettingsDialog::showDevices(unsigned request, bool ok, const std::vector<GleemDevice> &devices,
				 const std::string &error)
{
	if (request != deviceRequest)
		return;
	refreshButton->setEnabled(true);

	if (!ok) {
		deviceHint->setText(tr_("IrlControl.Settings.Device.Failed").arg(QString::fromStdString(error)));
		return;
	}
	setDeviceItems(devices);
	deviceHint->setText(devices.empty() ? tr_("IrlControl.Settings.Device.None")
					    : tr_("IrlControl.Settings.Publisher.Gleem.Hint"));
}

void SettingsDialog::setDeviceItems(const std::vector<GleemDevice> &devices)
{
	const QSignalBlocker blocker(deviceCombo);
	deviceCombo->clear();
	deviceCombo->addItem(tr_("IrlControl.Settings.Device.First"), QString());

	for (const GleemDevice &device : devices) {
		const QString uuid = QString::fromStdString(device.uuid);
		QString label = device.name.empty() ? uuid : QString::fromStdString(device.name);
		if (!device.online)
			label += " " + tr_("IrlControl.Settings.Device.Offline");
		deviceCombo->addItem(label, uuid);
		deviceCombo->setItemData(deviceCombo->count() - 1, uuid, Qt::ToolTipRole);
	}

	// Keep a configured device that is not (or not yet) in the list selectable,
	// so saving the dialog never silently drops it.
	int index = deviceCombo->findData(wantedDevice);
	if (index < 0) {
		deviceCombo->addItem(wantedDevice, wantedDevice);
		index = deviceCombo->count() - 1;
	}
	deviceCombo->setCurrentIndex(index);
}

QString SettingsDialog::selectedDevice() const
{
	return deviceCombo->currentData().toString();
}

void SettingsDialog::populateScenes(QComboBox *combo)
{
	// The returned list is one contiguous allocation; a single bfree() releases it.
	char **names = obs_frontend_get_scene_names();
	for (char **name = names; name && *name; ++name)
		combo->addItem(QString::fromUtf8(*name));
	bfree(names);
}

void SettingsDialog::populateTextSources(QComboBox *combo)
{
	obs_enum_sources(enum_text_sources, combo);
}

void SettingsDialog::selectOrInsert(QComboBox *combo, const QString &value)
{
	int index = combo->findText(value);
	if (index < 0) {
		combo->addItem(value);
		index = combo->count() - 1;
	}
	combo->setCurrentIndex(index);
}
