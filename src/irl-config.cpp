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

#include "irl-config.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <cstring>

const char *IrlConfig::typeToString(StatsServerType type)
{
	return type == StatsServerType::SrtRelay ? "srtrelay" : "belabox_cloud";
}

StatsServerType IrlConfig::typeFromString(const char *value)
{
	if (value && strcmp(value, "srtrelay") == 0)
		return StatsServerType::SrtRelay;
	return StatsServerType::BelaboxCloud;
}

void IrlConfig::fromData(obs_data_t *data)
{
	const IrlConfig defaults;

	obs_data_set_default_string(data, "stats_server_type", typeToString(defaults.statsType));
	obs_data_set_default_string(data, "stats_server_url", defaults.statsUrl.c_str());
	obs_data_set_default_string(data, "stats_server_publisher", defaults.publisher.c_str());
	obs_data_set_default_string(data, "scene_normal", defaults.normalScene.c_str());
	obs_data_set_default_string(data, "scene_offline", defaults.offlineScene.c_str());
	obs_data_set_default_string(data, "source_info", defaults.infoSource.c_str());
	obs_data_set_default_string(data, "source_stats", defaults.statsSource.c_str());
	obs_data_set_default_int(data, "warn_ms_rtt", defaults.warnMsRtt);
	obs_data_set_default_int(data, "max_ms_rtt", defaults.maxMsRtt);
	obs_data_set_default_int(data, "offline_threshold_sec", defaults.offlineThresholdSec);
	obs_data_set_default_int(data, "interval_ms", defaults.intervalMs);
	obs_data_set_default_bool(data, "start_paused", defaults.startPaused);

	statsType = typeFromString(obs_data_get_string(data, "stats_server_type"));
	statsUrl = obs_data_get_string(data, "stats_server_url");
	publisher = obs_data_get_string(data, "stats_server_publisher");
	normalScene = obs_data_get_string(data, "scene_normal");
	offlineScene = obs_data_get_string(data, "scene_offline");
	infoSource = obs_data_get_string(data, "source_info");
	statsSource = obs_data_get_string(data, "source_stats");
	warnMsRtt = (int)obs_data_get_int(data, "warn_ms_rtt");
	maxMsRtt = (int)obs_data_get_int(data, "max_ms_rtt");
	offlineThresholdSec = (int)obs_data_get_int(data, "offline_threshold_sec");
	intervalMs = (int)obs_data_get_int(data, "interval_ms");
	startPaused = obs_data_get_bool(data, "start_paused");

	if (intervalMs < 250)
		intervalMs = 250;
	if (offlineThresholdSec < 0)
		offlineThresholdSec = 0;
}

void IrlConfig::toData(obs_data_t *data) const
{
	obs_data_set_string(data, "stats_server_type", typeToString(statsType));
	obs_data_set_string(data, "stats_server_url", statsUrl.c_str());
	obs_data_set_string(data, "stats_server_publisher", publisher.c_str());
	obs_data_set_string(data, "scene_normal", normalScene.c_str());
	obs_data_set_string(data, "scene_offline", offlineScene.c_str());
	obs_data_set_string(data, "source_info", infoSource.c_str());
	obs_data_set_string(data, "source_stats", statsSource.c_str());
	obs_data_set_int(data, "warn_ms_rtt", warnMsRtt);
	obs_data_set_int(data, "max_ms_rtt", maxMsRtt);
	obs_data_set_int(data, "offline_threshold_sec", offlineThresholdSec);
	obs_data_set_int(data, "interval_ms", intervalMs);
	obs_data_set_bool(data, "start_paused", startPaused);
}

std::string irl_config_path()
{
	char *path = obs_module_config_path("config.json");
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

obs_data_t *irl_config_read()
{
	const std::string path = irl_config_path();
	obs_data_t *data = path.empty() ? nullptr : obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data)
		data = obs_data_create();
	return data;
}

bool irl_config_write(obs_data_t *data)
{
	char *dir = obs_module_config_path(nullptr);
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	const std::string path = irl_config_path();
	if (path.empty())
		return false;
	return obs_data_save_json_safe(data, path.c_str(), "tmp", "bak");
}
