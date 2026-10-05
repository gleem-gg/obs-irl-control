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

#include <cstdlib>
#include <string>

enum class StatsServerType { SrtRelay, BelaboxCloud, Gleem };

// Gleem's Developer API asks pollers to stay at or above this interval.
constexpr int GLEEM_MIN_INTERVAL_MS = 2000;
constexpr const char *GLEEM_DEFAULT_URL = "https://gleem.gg";

// An environment variable, or empty when unset.
inline std::string irl_env(const char *name)
{
	const char *value = std::getenv(name);
	return value ? value : "";
}

// Mirrors the config.json of the original IRL Control Node.js app.
struct IrlConfig {
	// Gleem only: a token and API URL handed to OBS by its environment, as on a
	// rented Gleem OBS machine. They take precedence over the configured ones (the
	// settings hide those fields then), are held in memory only and never written
	// to config.json, since the token ends with the rental.
	std::string envToken = irl_env("GLEEM_API_TOKEN");
	std::string envUrl = irl_env("GLEEM_API_URL");

	// With a token from the environment there is nothing left to configure, so
	// Gleem is the default there.
	StatsServerType statsType = envToken.empty() ? StatsServerType::BelaboxCloud : StatsServerType::Gleem;
	// srtrelay / Belabox: the stats URL. Gleem: the API base URL (empty = https://gleem.gg).
	std::string statsUrl;
	// srtrelay: stream id prefix. Belabox: publisher name. Gleem: device uuid (empty = first device).
	// "live" is Belabox's default, which as a Gleem device would never be found.
	std::string publisher = envToken.empty() ? "live" : "";
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

	// The Gleem token actually used: the environment's, else the configured one.
	const std::string &gleemToken() const { return usesEnvToken() ? envToken : apiToken; }
	bool usesEnvToken() const { return !envToken.empty(); }

	// The Gleem API base URL actually used (empty = https://gleem.gg). The
	// environment's token belongs to the environment's URL.
	const std::string &gleemUrl() const { return usesEnvToken() ? envUrl : statsUrl; }

	bool isConfigured() const
	{
		return statsType == StatsServerType::Gleem ? !gleemToken().empty() : !statsUrl.empty();
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
