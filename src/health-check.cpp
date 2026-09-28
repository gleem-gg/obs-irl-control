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

#include "health-check.hpp"

#include <obs-module.h>

#define LOG_PREFIX "[" PLUGIN_NAME "] "

HealthCheck::HealthCheck(const IrlConfig &cfg, Callbacks cbs)
	: config(cfg),
	  callbacks(std::move(cbs)),
	  server(cfg)
{
	pausedFlag = cfg.startPaused;
	snap.paused = cfg.startPaused;
}

HealthCheck::~HealthCheck()
{
	stop();
}

void HealthCheck::start()
{
	if (thread.joinable())
		return;
	stopFlag = false;
	{
		std::lock_guard<std::mutex> lock(snapshotMutex);
		snap.running = true;
	}
	thread = std::thread(&HealthCheck::run, this);
}

void HealthCheck::stop()
{
	{
		std::lock_guard<std::mutex> lock(waitMutex);
		stopFlag = true;
	}
	waitCv.notify_all();
	if (thread.joinable())
		thread.join();
	std::lock_guard<std::mutex> lock(snapshotMutex);
	snap.running = false;
}

void HealthCheck::setPaused(bool paused)
{
	pausedFlag = paused;
	{
		std::lock_guard<std::mutex> lock(snapshotMutex);
		snap.paused = paused;
	}
	blog(LOG_INFO, LOG_PREFIX "Health check %s", paused ? "paused" : "resumed");
}

HealthSnapshot HealthCheck::snapshot() const
{
	std::lock_guard<std::mutex> lock(snapshotMutex);
	return snap;
}

void HealthCheck::run()
{
	blog(LOG_INFO, LOG_PREFIX "Health check started (%s, every %d ms)",
	     IrlConfig::typeToString(config.statsType), config.effectiveIntervalMs());

	std::unique_lock<std::mutex> lock(waitMutex);
	while (!stopFlag) {
		lock.unlock();
		if (!pausedFlag)
			tick();
		lock.lock();
		waitCv.wait_for(lock, std::chrono::milliseconds(config.effectiveIntervalMs()), [this] { return stopFlag.load(); });
	}

	blog(LOG_INFO, LOG_PREFIX "Health check stopped");
}

void HealthCheck::tick()
{
	std::string error;
	const std::optional<StreamStats> stats = server.fetch(error);

	{
		std::lock_guard<std::mutex> lock(snapshotMutex);
		snap.lastError = error;
	}

	if (stats) {
		if (stats->msRtt > config.maxMsRtt) {
			blog(LOG_INFO, LOG_PREFIX "Stream is unhealthy, RTT is too high: %.0f ms", stats->msRtt);
			handleUnhealthy(&*stats);
		} else {
			handleHealthy(*stats);
		}
	} else {
		handleUnhealthy(nullptr);
	}
}

void HealthCheck::handleHealthy(const StreamStats &stats)
{
	offlineStart.reset();

	if (stats.msRtt > config.warnMsRtt)
		blog(LOG_INFO, LOG_PREFIX "Stream is live, RTT is high: %.0f ms", stats.msRtt);
	else
		blog(LOG_DEBUG, LOG_PREFIX "Stream is live, RTT is good: %.0f ms", stats.msRtt);

	{
		std::lock_guard<std::mutex> lock(snapshotMutex);
		snap.state = StreamState::Online;
		snap.msRtt = stats.msRtt;
		snap.offlineDuration = 0;
		snap.statsText = stats.joined();
	}

	if (callbacks.onlineHeartbeat)
		callbacks.onlineHeartbeat(stats);

	if (markedOffline) {
		markedOffline = false;
		{
			std::lock_guard<std::mutex> lock(snapshotMutex);
			snap.markedOffline = false;
		}
		blog(LOG_INFO, LOG_PREFIX "Stream has reconnected");
		if (callbacks.streamReconnected)
			callbacks.streamReconnected();
	}
}

void HealthCheck::handleUnhealthy(const StreamStats *stats)
{
	const auto now = std::chrono::steady_clock::now();
	if (!offlineStart)
		offlineStart = now;

	const int offlineDuration = (int)std::chrono::duration_cast<std::chrono::seconds>(now - *offlineStart).count();
	blog(LOG_DEBUG, LOG_PREFIX "Stream is offline for %d seconds", offlineDuration);

	{
		std::lock_guard<std::mutex> lock(snapshotMutex);
		snap.state = StreamState::Offline;
		snap.msRtt = stats ? stats->msRtt : 0.0;
		snap.offlineDuration = offlineDuration;
		snap.statsText = stats ? stats->joined() : std::string();
	}

	if (callbacks.offlineHeartbeat)
		callbacks.offlineHeartbeat(stats, offlineDuration);

	if (offlineDuration >= config.offlineThresholdSec && !markedOffline) {
		markedOffline = true;
		{
			std::lock_guard<std::mutex> lock(snapshotMutex);
			snap.markedOffline = true;
		}
		blog(LOG_INFO, LOG_PREFIX "Stream has been offline for %d seconds", offlineDuration);
		if (callbacks.streamOffline)
			callbacks.streamOffline(offlineDuration);
	}
}
