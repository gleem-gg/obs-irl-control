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

enum class StatsServerType { SrtRelay, BelaboxCloud, Gleem };

// Gleem's Developer API asks pollers to stay at or above this interval.
constexpr int GLEEM_MIN_INTERVAL_MS = 2000;
constexpr const char *GLEEM_DEFAULT_URL = "https://gleem.gg";

// Mirrors the config.json of the original IRL Control Node.js app.
struct IrlConfig {
	StatsServerType statsType = StatsServerType::BelaboxCloud;
	// srtrelay / Belabox: the stats URL. Gleem: the API base URL (empty = https://gleem.gg).
	std::string statsUrl;
	// srtrelay: stream id prefix. Belabox: publisher name. Gleem: device uuid (empty = first device).
	std::string publisher = "live";
	// Gleem only: a Developer API token with the irl:read scope.
	std::string apiToken;

	std::string normalScene = "Live";
	std::string offlineScene = "Disconnected";

	std::string infoSource = "Info";
	std::string statsSource = "Stats";

	int warnMsRtt = 500;
	int maxMsRtt = 2000;
	int offlineThresholdSec = 5;
	int intervalMs = 1000;
	bool startPaused = false;

	bool isConfigured() const
	{
		return statsType == StatsServerType::Gleem ? !apiToken.empty() : !statsUrl.empty();
	}

	// The polling interval actually used (Gleem enforces a minimum).
	int effectiveIntervalMs() const
	{
		return statsType == StatsServerType::Gleem && intervalMs < GLEEM_MIN_INTERVAL_MS ? GLEEM_MIN_INTERVAL_MS
												  : intervalMs;
	}

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
