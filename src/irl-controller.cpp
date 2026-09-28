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

#include "irl-controller.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include "obs-websocket-api.h"

#define LOG_PREFIX "[" PLUGIN_NAME "] "

namespace {

const char *const HOTKEY_NAMES[] = {
	"irl_control.pause", "irl_control.resume", "irl_control.toggle", "irl_control.force_online",
	"irl_control.force_offline",
};

const char *const HOTKEY_LOCALE[] = {
	"IrlControl.Hotkey.Pause", "IrlControl.Hotkey.Resume", "IrlControl.Hotkey.Toggle",
	"IrlControl.Hotkey.ForceOnline", "IrlControl.Hotkey.ForceOffline",
};

struct SceneTask {
	std::string name;
};

void switch_scene_task(void *param)
{
	auto *task = static_cast<SceneTask *>(param);
	obs_source_t *scene = obs_get_source_by_name(task->name.c_str());
	if (scene) {
		obs_frontend_set_current_scene(scene);
		obs_source_release(scene);
	} else {
		blog(LOG_WARNING, LOG_PREFIX "Scene '%s' not found, cannot switch", task->name.c_str());
	}
	delete task;
}

const char *state_to_string(StreamState state)
{
	switch (state) {
	case StreamState::Online:
		return "online";
	case StreamState::Offline:
		return "offline";
	default:
		return "unknown";
	}
}

void fill_status(obs_data_t *out, IrlController &ctl)
{
	const HealthSnapshot s = ctl.snapshot();
	obs_data_set_bool(out, "configured", ctl.isConfigured());
	obs_data_set_bool(out, "running", s.running);
	obs_data_set_bool(out, "paused", s.paused);
	obs_data_set_string(out, "state", state_to_string(s.state));
	obs_data_set_bool(out, "marked_offline", s.markedOffline);
	obs_data_set_double(out, "ms_rtt", s.msRtt);
	obs_data_set_int(out, "offline_duration", s.offlineDuration);
	obs_data_set_string(out, "stats", s.statsText.c_str());
	obs_data_set_string(out, "last_error", s.lastError.c_str());
}

// obs-websocket vendor request handlers -------------------------------------------------

void vendor_get_status(obs_data_t *, obs_data_t *response, void *priv)
{
	fill_status(response, *static_cast<IrlController *>(priv));
}

void vendor_pause(obs_data_t *, obs_data_t *response, void *priv)
{
	auto *ctl = static_cast<IrlController *>(priv);
	ctl->pause();
	fill_status(response, *ctl);
}

void vendor_resume(obs_data_t *, obs_data_t *response, void *priv)
{
	auto *ctl = static_cast<IrlController *>(priv);
	ctl->resume();
	fill_status(response, *ctl);
}

void vendor_toggle_pause(obs_data_t *, obs_data_t *response, void *priv)
{
	auto *ctl = static_cast<IrlController *>(priv);
	ctl->togglePause();
	fill_status(response, *ctl);
}

void vendor_force_online(obs_data_t *, obs_data_t *response, void *priv)
{
	auto *ctl = static_cast<IrlController *>(priv);
	ctl->forceOnline();
	fill_status(response, *ctl);
}

void vendor_force_offline(obs_data_t *, obs_data_t *response, void *priv)
{
	auto *ctl = static_cast<IrlController *>(priv);
	ctl->forceOffline();
	fill_status(response, *ctl);
}

void vendor_get_config(obs_data_t *, obs_data_t *response, void *priv)
{
	static_cast<IrlController *>(priv)->config().toData(response);
}

} // namespace

IrlController &IrlController::instance()
{
	static IrlController controller;
	return controller;
}

IrlController::~IrlController()
{
	if (health)
		health->stop();
}

void IrlController::load()
{
	if (loaded)
		return;
	loaded = true;

	obs_data_t *data = irl_config_read();
	{
		std::lock_guard<std::mutex> lock(configMutex);
		cfg.fromData(data);
	}
	registerHotkeys(data);
	obs_data_release(data);

	blog(LOG_INFO, LOG_PREFIX "Config loaded from %s", irl_config_path().c_str());
}

void IrlController::start()
{
	restartHealthCheck();
}

void IrlController::shutdown()
{
	if (shutDown)
		return;
	shutDown = true;
	if (health) {
		health->stop();
		health.reset();
	}
	save();
}

IrlConfig IrlController::config() const
{
	std::lock_guard<std::mutex> lock(configMutex);
	return cfg;
}

void IrlController::applyConfig(const IrlConfig &config)
{
	// Stop the worker first so the callbacks never observe a half-updated config.
	if (health) {
		health->stop();
		health.reset();
	}
	{
		std::lock_guard<std::mutex> lock(configMutex);
		cfg = config;
	}
	{
		std::lock_guard<std::mutex> lock(textMutex);
		lastText.clear();
	}
	save();
	if (!shutDown)
		restartHealthCheck();
}

bool IrlController::isConfigured() const
{
	std::lock_guard<std::mutex> lock(configMutex);
	return cfg.isConfigured();
}

HealthSnapshot IrlController::snapshot() const
{
	if (health)
		return health->snapshot();
	HealthSnapshot s;
	s.paused = config().startPaused;
	return s;
}

void IrlController::pause()
{
	if (!health || health->paused())
		return;
	health->setPaused(true);
	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "paused", true);
	emitEvent("PausedChanged", data);
	obs_data_release(data);
}

void IrlController::resume()
{
	if (!health || !health->paused())
		return;
	health->setPaused(false);
	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "paused", false);
	emitEvent("PausedChanged", data);
	obs_data_release(data);
}

void IrlController::togglePause()
{
	if (isPaused())
		resume();
	else
		pause();
}

bool IrlController::isPaused() const
{
	return health ? health->paused() : false;
}

void IrlController::forceOnline()
{
	blog(LOG_INFO, LOG_PREFIX "Manually switching to online scene");
	switchScene(config().normalScene);
}

void IrlController::forceOffline()
{
	blog(LOG_INFO, LOG_PREFIX "Manually switching to offline scene");
	switchScene(config().offlineScene);
}

void IrlController::restartHealthCheck()
{
	if (health) {
		health->stop();
		health.reset();
	}

	const IrlConfig current = config();
	if (!current.isConfigured()) {
		blog(LOG_WARNING, LOG_PREFIX "No stats server URL configured, health check not started");
		return;
	}

	HealthCheck::Callbacks cbs;

	cbs.onlineHeartbeat = [this](const StreamStats &stats) {
		const IrlConfig c = config();
		setText(c.infoSource, "Stream: Connected");
		setText(c.statsSource, stats.joined());
	};

	cbs.offlineHeartbeat = [this](const StreamStats *stats, int offlineSeconds) {
		const IrlConfig c = config();
		setText(c.infoSource, "Stream: Disconnected (" + std::to_string(offlineSeconds) + "s)");
		setText(c.statsSource, stats ? stats->joined() : std::string());
	};

	cbs.streamOffline = [this](int offlineSeconds) {
		switchScene(config().offlineScene);
		obs_data_t *data = obs_data_create();
		obs_data_set_int(data, "offline_duration", offlineSeconds);
		emitEvent("StreamOffline", data);
		obs_data_release(data);
	};

	cbs.streamReconnected = [this]() {
		switchScene(config().normalScene);
		obs_data_t *data = obs_data_create();
		emitEvent("StreamReconnected", data);
		obs_data_release(data);
	};

	health = std::make_unique<HealthCheck>(current, std::move(cbs));
	health->start();
}

void IrlController::save()
{
	obs_data_t *data = obs_data_create();
	config().toData(data);
	saveHotkeys(data);
	if (!irl_config_write(data))
		blog(LOG_WARNING, LOG_PREFIX "Failed to write config to %s", irl_config_path().c_str());
	obs_data_release(data);
}

void IrlController::switchScene(const std::string &sceneName)
{
	if (sceneName.empty())
		return;
	// Scene switching must happen on the UI thread.
	obs_queue_task(OBS_TASK_UI, switch_scene_task, new SceneTask{sceneName}, false);
}

void IrlController::setText(const std::string &sourceName, const std::string &text)
{
	if (sourceName.empty())
		return;

	{
		// Skip the update when nothing changed; text sources re-render on every update.
		std::lock_guard<std::mutex> lock(textMutex);
		auto it = lastText.find(sourceName);
		if (it != lastText.end() && it->second == text)
			return;
		lastText[sourceName] = text;
	}

	obs_source_t *source = obs_get_source_by_name(sourceName.c_str());
	if (!source) {
		blog(LOG_DEBUG, LOG_PREFIX "Text source '%s' not found", sourceName.c_str());
		return;
	}
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.c_str());
	obs_source_update(source, settings);
	obs_data_release(settings);
	obs_source_release(source);
}

void IrlController::emitEvent(const char *eventName, obs_data_t *data)
{
	if (vendor)
		obs_websocket_vendor_emit_event(vendor, eventName, data);
}

// Hotkeys ---------------------------------------------------------------------------------

void IrlController::hotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *self = static_cast<IrlController *>(data);
	if (id == self->hotkeys[HK_PAUSE])
		self->pause();
	else if (id == self->hotkeys[HK_RESUME])
		self->resume();
	else if (id == self->hotkeys[HK_TOGGLE])
		self->togglePause();
	else if (id == self->hotkeys[HK_FORCE_ONLINE])
		self->forceOnline();
	else if (id == self->hotkeys[HK_FORCE_OFFLINE])
		self->forceOffline();
}

void IrlController::registerHotkeys(obs_data_t *saved)
{
	for (int i = 0; i < HK_COUNT; i++) {
		hotkeys[i] = obs_hotkey_register_frontend(HOTKEY_NAMES[i], obs_module_text(HOTKEY_LOCALE[i]), hotkeyCallback,
							  this);
		obs_data_array_t *bindings = obs_data_get_array(saved, HOTKEY_NAMES[i]);
		if (bindings) {
			obs_hotkey_load(hotkeys[i], bindings);
			obs_data_array_release(bindings);
		}
	}
}

void IrlController::saveHotkeys(obs_data_t *out)
{
	for (int i = 0; i < HK_COUNT; i++) {
		if (hotkeys[i] == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *bindings = obs_hotkey_save(hotkeys[i]);
		if (bindings) {
			obs_data_set_array(out, HOTKEY_NAMES[i], bindings);
			obs_data_array_release(bindings);
		}
	}
}

// obs-websocket vendor ---------------------------------------------------------------------

void IrlController::registerWebsocketVendor()
{
	vendor = obs_websocket_register_vendor("irl-control");
	if (!vendor) {
		blog(LOG_INFO, LOG_PREFIX "obs-websocket not available, vendor requests disabled");
		return;
	}

	obs_websocket_vendor_register_request(vendor, "GetStatus", vendor_get_status, this);
	obs_websocket_vendor_register_request(vendor, "GetConfig", vendor_get_config, this);
	obs_websocket_vendor_register_request(vendor, "Pause", vendor_pause, this);
	obs_websocket_vendor_register_request(vendor, "Resume", vendor_resume, this);
	obs_websocket_vendor_register_request(vendor, "TogglePause", vendor_toggle_pause, this);
	obs_websocket_vendor_register_request(vendor, "ForceOnline", vendor_force_online, this);
	obs_websocket_vendor_register_request(vendor, "ForceOffline", vendor_force_offline, this);

	blog(LOG_INFO, LOG_PREFIX "Registered obs-websocket vendor 'irl-control'");
}
