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

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <curl/curl.h>

#include "irl-controller.hpp"
#include "irl-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "IRL Control";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Automatically switches scenes depending on the health of an IRL SRT stream "
	       "(srtrelay or Belabox Cloud).";
}

static void frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		IrlController::instance().start();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		IrlController::instance().shutdown();
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);

	IrlController::instance().load();

	// OBS takes ownership of the dock widget.
	auto *dock = new IrlDock();
	obs_frontend_add_dock_by_id("irl-control-dock", obs_module_text("IrlControl.Dock.Title"), dock);

	obs_frontend_add_event_callback(frontend_event, nullptr);

	blog(LOG_INFO, "[%s] loaded version %s", PLUGIN_NAME, PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	IrlController::instance().registerWebsocketVendor();
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	IrlController::instance().shutdown();
	curl_global_cleanup();
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_NAME);
}
