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

#include "stats-server.hpp"

#include <obs-module.h>
#include <curl/curl.h>

#include <cmath>
#include <cstdio>

namespace {

size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *body = static_cast<std::string *>(userdata);
	body->append(ptr, size * nmemb);
	return size * nmemb;
}

// Copies every numeric (non-zero) entry of `obj` into `out.values`.
void collect_numbers(obs_data_t *obj, StreamStats &out)
{
	for (obs_data_item_t *item = obs_data_first(obj); item; obs_data_item_next(&item)) {
		if (obs_data_item_gettype(item) != OBS_DATA_NUMBER)
			continue;
		const double value = obs_data_item_get_double(item);
		if (value > 0.0)
			out.values.emplace_back(obs_data_item_get_name(item), value);
	}
}

std::string format_number(double value)
{
	char buf[64];
	if (std::floor(value) == value)
		snprintf(buf, sizeof(buf), "%.0f", value);
	else
		snprintf(buf, sizeof(buf), "%.2f", value);
	return buf;
}

} // namespace

std::string StreamStats::joined(size_t perLine) const
{
	std::string result;
	size_t inLine = 0;
	for (const auto &[key, value] : values) {
		if (inLine == 0 && !result.empty())
			result += '\n';
		else if (inLine > 0)
			result += ", ";
		result += key + ": " + format_number(value);
		if (++inLine >= perLine)
			inLine = 0;
	}
	return result;
}

StatsServer::StatsServer(const IrlConfig &config)
	: type(config.statsType),
	  url(config.statsUrl),
	  publisher(config.publisher),
	  // Never let a single request outlive the polling interval by much.
	  timeoutMs(config.intervalMs > 1000 ? config.intervalMs : 1000)
{
}

std::optional<StreamStats> StatsServer::fetch(std::string &error) const
{
	error.clear();
	if (url.empty()) {
		error = "Stats server URL is not configured";
		return std::nullopt;
	}

	std::string requestUrl = url;
	if (type == StatsServerType::SrtRelay) {
		if (!requestUrl.empty() && requestUrl.back() == '/')
			requestUrl.pop_back();
		requestUrl += "/sockets";
	}

	std::string body;
	if (!httpGet(requestUrl, body, error))
		return std::nullopt;

	if (type == StatsServerType::SrtRelay)
		return parseSrtRelay(body, error);
	return parseBelaboxCloud(body, error);
}

bool StatsServer::httpGet(const std::string &requestUrl, std::string &body, std::string &error) const
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		error = "Failed to initialise libcurl";
		return false;
	}

	char errbuf[CURL_ERROR_SIZE] = {0};
	curl_easy_setopt(curl, CURLOPT_URL, requestUrl.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs * 2);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, PLUGIN_NAME "/" PLUGIN_VERSION);

	const CURLcode res = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		error = errbuf[0] ? errbuf : curl_easy_strerror(res);
		return false;
	}
	if (status < 200 || status >= 300) {
		error = "Stats server responded with HTTP " + std::to_string(status);
		return false;
	}
	return true;
}

std::optional<StreamStats> StatsServer::parseSrtRelay(const std::string &body, std::string &error) const
{
	// srtrelay answers with a top-level JSON array; obs_data only parses objects, so wrap it.
	const std::string wrapped = "{\"sockets\":" + body + "}";
	obs_data_t *root = obs_data_create_from_json(wrapped.c_str());
	if (!root) {
		error = "Invalid JSON from srtrelay";
		return std::nullopt;
	}

	std::optional<StreamStats> result;
	obs_data_array_t *sockets = obs_data_get_array(root, "sockets");
	const size_t count = obs_data_array_count(sockets);
	for (size_t i = 0; i < count && !result; i++) {
		obs_data_t *socket = obs_data_array_item(sockets, i);
		const char *streamId = obs_data_get_string(socket, "stream_id");
		if (streamId && std::string(streamId).rfind(publisher, 0) == 0) {
			obs_data_t *stats = obs_data_get_obj(socket, "stats");
			if (stats) {
				StreamStats s;
				s.msRtt = obs_data_get_double(stats, "MsRTT");
				collect_numbers(stats, s);
				result = std::move(s);
				obs_data_release(stats);
			}
		}
		obs_data_release(socket);
	}
	obs_data_array_release(sockets);
	obs_data_release(root);
	return result;
}

std::optional<StreamStats> StatsServer::parseBelaboxCloud(const std::string &body, std::string &error) const
{
	obs_data_t *root = obs_data_create_from_json(body.c_str());
	if (!root) {
		error = "Invalid JSON from Belabox Cloud";
		return std::nullopt;
	}

	std::optional<StreamStats> result;
	obs_data_t *publishers = obs_data_get_obj(root, "publishers");
	obs_data_t *pub = publishers ? obs_data_get_obj(publishers, publisher.c_str()) : nullptr;
	if (!pub) {
		error = "Publisher '" + publisher + "' not found in Belabox Cloud stats";
	} else if (obs_data_get_bool(pub, "connected")) {
		StreamStats s;
		s.msRtt = obs_data_get_double(pub, "rtt");
		collect_numbers(pub, s);
		result = std::move(s);
	}

	obs_data_release(pub);
	obs_data_release(publishers);
	obs_data_release(root);
	return result;
}
