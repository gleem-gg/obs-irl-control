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

#include "irl-config.hpp"
#include "stats-server.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

enum class StreamState { Unknown, Online, Offline };

// Thread-safe copy of the health check state for display purposes.
struct HealthSnapshot {
	bool running = false;
	bool paused = false;
	StreamState state = StreamState::Unknown;
	bool markedOffline = false;
	double msRtt = 0.0;
	int offlineDuration = 0;
	std::string statsText;
	std::string lastError;
};

// Polls the stats server on a background thread and tracks whether the IRL stream is
// healthy. This is a direct port of useHealthCheck.ts from the Node.js app.
class HealthCheck {
public:
	struct Callbacks {
		// Fired on every tick while the stream is healthy.
		std::function<void(const StreamStats &)> onlineHeartbeat;
		// Fired on every tick while the stream is unhealthy (stats may be null).
		std::function<void(const StreamStats *, int offlineSeconds)> offlineHeartbeat;
		// Fired once when the stream has been unhealthy for longer than the threshold.
		std::function<void(int offlineSeconds)> streamOffline;
		// Fired once when the stream is healthy again after being marked offline.
		std::function<void()> streamReconnected;
	};

	HealthCheck(const IrlConfig &config, Callbacks callbacks);
	~HealthCheck();

	HealthCheck(const HealthCheck &) = delete;
	HealthCheck &operator=(const HealthCheck &) = delete;

	void start();
	void stop();

	void setPaused(bool paused);
	bool paused() const { return pausedFlag.load(); }

	HealthSnapshot snapshot() const;

private:
	void run();
	void tick();
	void handleHealthy(const StreamStats &stats);
	void handleUnhealthy(const StreamStats *stats);

	IrlConfig config;
	Callbacks callbacks;
	StatsServer server;

	std::thread thread;
	std::mutex waitMutex;
	std::condition_variable waitCv;
	std::atomic<bool> stopFlag{false};
	std::atomic<bool> pausedFlag{false};

	// Worker-thread only state (mirrors `state` in useHealthCheck.ts).
	std::optional<std::chrono::steady_clock::time_point> offlineStart;
	bool markedOffline = false;

	mutable std::mutex snapshotMutex;
	HealthSnapshot snap;
};
