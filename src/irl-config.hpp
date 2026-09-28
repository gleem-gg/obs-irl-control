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

#include <obs-data.h>

#include <string>

enum class StatsServerType { SrtRelay, BelaboxCloud };

// Mirrors the config.json of the original IRL Control Node.js app.
struct IrlConfig {
	StatsServerType statsType = StatsServerType::BelaboxCloud;
	std::string statsUrl;
	std::string publisher = "live";

	std::string normalScene = "Live";
	std::string offlineScene = "Disconnected";

	std::string infoSource = "Info";
	std::string statsSource = "Stats";

	int warnMsRtt = 500;
	int maxMsRtt = 2000;
	int offlineThresholdSec = 5;
	int intervalMs = 1000;
	bool startPaused = false;

	bool isConfigured() const { return !statsUrl.empty(); }

	static const char *typeToString(StatsServerType type);
	static StatsServerType typeFromString(const char *value);

	// Reads the settings from an obs_data object (defaults are applied for missing keys).
	void fromData(obs_data_t *data);
	// Writes the settings into an obs_data object.
	void toData(obs_data_t *data) const;
};

// Location of the plugin's config file inside the OBS module config directory.
std::string irl_config_path();

// Reads the config file. Never returns null; caller must obs_data_release().
obs_data_t *irl_config_read();

// Writes the config file (creating the directory if needed).
bool irl_config_write(obs_data_t *data);
