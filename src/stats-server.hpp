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

#include <optional>
#include <string>
#include <utility>
#include <vector>

// A single sample of stream statistics as reported by the stats server.
struct StreamStats {
	double msRtt = 0.0;
	// Every numeric, non-zero statistic in the order the server reported it.
	std::vector<std::pair<std::string, double>> values;

	// Formats the values like the Node.js app did: "key: value" entries, three per line.
	std::string joined(size_t perLine = 3) const;
};

// Fetches stream statistics from an srtrelay or Belabox Cloud stats endpoint, or the
// stream health of a Gleem IRL box from the gleem.gg Developer API.
class StatsServer {
public:
	explicit StatsServer(const IrlConfig &config);

	// Returns the stats of the configured publisher, or nullopt when the publisher is
	// not connected or the request failed. `error` describes a failure (empty otherwise).
	// `unauthorized` is set when the server refused the credentials, which says
	// nothing about the stream itself.
	std::optional<StreamStats> fetch(std::string &error, bool &unauthorized) const;

private:
	// Transport only: true when a response arrived, whatever its status.
	bool httpGet(const std::string &url, const std::vector<std::string> &headers, std::string &body, long &status,
		     std::string &error) const;
	std::optional<StreamStats> parseSrtRelay(const std::string &body, std::string &error) const;
	std::optional<StreamStats> parseBelaboxCloud(const std::string &body, std::string &error) const;
	std::optional<StreamStats> fetchGleem(std::string &error, bool &unauthorized) const;

	StatsServerType type;
	std::string url;
	std::string publisher;
	std::string apiToken;
	long timeoutMs;
};
