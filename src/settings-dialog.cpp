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

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cstring>

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
	tokenHint->setTextFormat(Qt::RichText);
	tokenHint->setOpenExternalLinks(true);
	serverForm->addRow(QString(), tokenHint);

	urlEdit = new QLineEdit(QString::fromStdString(config.statsUrl), serverBox);
	serverForm->addRow(tr_("IrlControl.Settings.Url"), urlEdit);
	urlHint = makeHint();
	serverForm->addRow(QString(), urlHint);

	publisherEdit = new QLineEdit(QString::fromStdString(config.publisher), serverBox);
	serverForm->addRow(tr_("IrlControl.Settings.Publisher"), publisherEdit);
	publisherHint = makeHint();
	serverForm->addRow(QString(), publisherHint);

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

	connect(typeCombo, &QComboBox::currentIndexChanged, this, [this]() { updateServerFields(); });
	updateServerFields();

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
	c.publisher = publisherEdit->text().trimmed().toStdString();
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

	auto setLabel = [this](QWidget *field, const char *key) {
		if (auto *label = qobject_cast<QLabel *>(serverForm->labelForField(field)))
			label->setText(tr_(key));
	};

	switch (type) {
	case StatsServerType::Gleem:
		setLabel(urlEdit, "IrlControl.Settings.Url.Gleem");
		setLabel(publisherEdit, "IrlControl.Settings.Publisher.Gleem");
		urlEdit->setPlaceholderText(QString::fromUtf8(GLEEM_DEFAULT_URL));
		publisherEdit->setPlaceholderText(tr_("IrlControl.Settings.Publisher.Gleem.Placeholder"));
		urlHint->setText(tr_("IrlControl.Settings.Url.Gleem.Hint"));
		publisherHint->setText(tr_("IrlControl.Settings.Publisher.Gleem.Hint"));
		break;
	case StatsServerType::SrtRelay:
		setLabel(urlEdit, "IrlControl.Settings.Url");
		setLabel(publisherEdit, "IrlControl.Settings.Publisher");
		urlEdit->setPlaceholderText("http://127.0.0.1:34101");
		publisherEdit->setPlaceholderText("publish/test/");
		urlHint->setText(tr_("IrlControl.Settings.Url.Hint"));
		publisherHint->setText(tr_("IrlControl.Settings.Publisher.Hint"));
		break;
	default:
		setLabel(urlEdit, "IrlControl.Settings.Url");
		setLabel(publisherEdit, "IrlControl.Settings.Publisher");
		urlEdit->setPlaceholderText("https://stats.srt.belabox.net/XXXXX");
		publisherEdit->setPlaceholderText("live");
		urlHint->setText(tr_("IrlControl.Settings.Url.Hint"));
		publisherHint->setText(tr_("IrlControl.Settings.Publisher.Hint"));
		break;
	}

	intervalSpin->setMinimum(gleem ? GLEEM_MIN_INTERVAL_MS : 250);
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
