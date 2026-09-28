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

// Fetches stream statistics from an srtrelay or Belabox Cloud stats endpoint.
class StatsServer {
public:
	explicit StatsServer(const IrlConfig &config);

	// Returns the stats of the configured publisher, or nullopt when the publisher is
	// not connected or the request failed. `error` describes a failure (empty otherwise).
	std::optional<StreamStats> fetch(std::string &error) const;

private:
	bool httpGet(const std::string &url, std::string &body, std::string &error) const;
	std::optional<StreamStats> parseSrtRelay(const std::string &body, std::string &error) const;
	std::optional<StreamStats> parseBelaboxCloud(const std::string &body, std::string &error) const;

	StatsServerType type;
	std::string url;
	std::string publisher;
	long timeoutMs;
};
