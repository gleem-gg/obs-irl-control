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

#include "health-check.hpp"
#include "irl-config.hpp"

#include <obs.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// Glue between the health check and OBS: scene switching, text sources, hotkeys and
// obs-websocket vendor requests. Lives for the lifetime of the module.
class IrlController {
public:
	static IrlController &instance();

	// obs_module_load: read config and register hotkeys.
	void load();
	// OBS_FRONTEND_EVENT_FINISHED_LOADING: start polling.
	void start();
	// obs_module_post_load: expose vendor requests to obs-websocket (if present).
	void registerWebsocketVendor();
	// OBS exit / module unload: stop polling and persist config + hotkeys.
	void shutdown();

	IrlConfig config() const;
	// Persists the new config and restarts the health check with it.
	void applyConfig(const IrlConfig &config);

	bool isConfigured() const;
	HealthSnapshot snapshot() const;

	void pause();
	void resume();
	void togglePause();
	bool isPaused() const;

	// Switch scenes manually without touching the health check state
	// (same semantics as `!irlc online` / `!irlc offline` in the Node.js app).
	void forceOnline();
	void forceOffline();

private:
	IrlController() = default;
	~IrlController();

	void restartHealthCheck();
	void save();

	void switchScene(const std::string &sceneName);
	void setText(const std::string &sourceName, const std::string &text);
	void emitEvent(const char *eventName, obs_data_t *data);

	void registerHotkeys(obs_data_t *saved);
	void saveHotkeys(obs_data_t *out);
	static void hotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	enum Hotkey { HK_PAUSE, HK_RESUME, HK_TOGGLE, HK_FORCE_ONLINE, HK_FORCE_OFFLINE, HK_COUNT };
	obs_hotkey_id hotkeys[HK_COUNT] = {};

	mutable std::mutex configMutex;
	IrlConfig cfg;
	std::unique_ptr<HealthCheck> health;

	std::mutex textMutex;
	std::unordered_map<std::string, std::string> lastText;

	void *vendor = nullptr;
	bool loaded = false;
	bool shutDown = false;
};
