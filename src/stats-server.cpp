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
	  url(config.statsType == StatsServerType::Gleem ? config.gleemUrl() : config.statsUrl),
	  publisher(config.publisher),
	  apiToken(config.gleemToken()),
	  // Never let a single request outlive the polling interval by much.
	  timeoutMs(config.effectiveIntervalMs() > 1000 ? config.effectiveIntervalMs() : 1000)
{
}

std::optional<StreamStats> StatsServer::fetch(std::string &error, bool &unauthorized) const
{
	error.clear();
	unauthorized = false;
	if (type == StatsServerType::Gleem)
		return fetchGleem(error, unauthorized);

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
	long status = 0;
	if (!httpGet(requestUrl, {}, body, status, error))
		return std::nullopt;
	if (status < 200 || status >= 300) {
		error = "Stats server responded with HTTP " + std::to_string(status);
		return std::nullopt;
	}

	if (type == StatsServerType::SrtRelay)
		return parseSrtRelay(body, error);
	return parseBelaboxCloud(body, error);
}

bool StatsServer::httpGet(const std::string &requestUrl, const std::vector<std::string> &headers, std::string &body,
			  long &status, std::string &error) const
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		error = "Failed to initialise libcurl";
		return false;
	}

	struct curl_slist *headerList = nullptr;
	for (const std::string &header : headers)
		headerList = curl_slist_append(headerList, header.c_str());

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
	if (headerList)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

	const CURLcode res = curl_easy_perform(curl);
	status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);
	curl_slist_free_all(headerList);

	if (res != CURLE_OK) {
		error = errbuf[0] ? errbuf : curl_easy_strerror(res);
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

// Gleem IRL ---------------------------------------------------------------------------------

namespace {

std::string gleem_api_error(const std::string &body, long status)
{
	std::string message;
	obs_data_t *root = obs_data_create_from_json(body.c_str());
	if (root) {
		obs_data_t *err = obs_data_get_obj(root, "error");
		if (err) {
			message = obs_data_get_string(err, "message");
			obs_data_release(err);
		}
		obs_data_release(root);
	}
	if (status == 401 && message.empty())
		message = "The Gleem API token is invalid, expired or revoked";
	if (message.empty())
		message = "Gleem API responded with HTTP " + std::to_string(status);
	return message;
}

// Turns one device object of the Developer API into stats, or explains why it is unhealthy.
std::optional<StreamStats> gleem_device_stats(obs_data_t *device, std::string &error)
{
	const std::string name = obs_data_get_string(device, "name");
	const std::string label = name.empty() ? "IRL Sidekick" : "IRL Sidekick '" + name + "'";

	if (!obs_data_get_bool(device, "online")) {
		error = label + " is offline";
		return std::nullopt;
	}

	obs_data_t *stream = obs_data_get_obj(device, "stream");
	if (!stream) {
		error = label + " is not streaming";
		return std::nullopt;
	}

	std::optional<StreamStats> result;
	const std::string state = obs_data_get_string(stream, "state");

	if (!obs_data_get_bool(stream, "healthy")) {
		obs_data_t *ingest = obs_data_get_obj(device, "ingest");
		if (state != "live")
			error = label + " stream is " + state;
		else if (!obs_data_get_bool(stream, "encoder_connected"))
			error = label + " encoder is not connected";
		else if (!ingest || !obs_data_get_bool(ingest, "publishing"))
			error = "Stream is not arriving at Gleem ingest";
		else
			error = "Stream has no live link";
		obs_data_release(ingest);
	} else {
		StreamStats s;
		s.msRtt = obs_data_get_double(stream, "rtt_ms");
		if (s.msRtt > 0.0)
			s.values.emplace_back("RTT", s.msRtt);
		const double bitrate = obs_data_get_double(stream, "bitrate_bps");
		if (bitrate > 0.0)
			s.values.emplace_back("Mbps", std::round(bitrate / 10000.0) / 100.0);

		obs_data_array_t *links = obs_data_get_array(stream, "links");
		const size_t count = obs_data_array_count(links);
		size_t live = 0;
		std::vector<std::pair<std::string, double>> perLink;
		for (size_t i = 0; i < count; i++) {
			obs_data_t *link = obs_data_array_item(links, i);
			const std::string iface = obs_data_get_string(link, "iface");
			if (std::string(obs_data_get_string(link, "state")) == "live") {
				live++;
				const std::string key = iface.empty() ? "link" + std::to_string(i) : iface;
				const double rtt = obs_data_get_double(link, "rtt_ms");
				const double loss = obs_data_get_double(link, "loss_pct");
				if (rtt > 0.0)
					perLink.emplace_back(key + " RTT", rtt);
				if (loss > 0.0)
					perLink.emplace_back(key + " loss%", loss);
			}
			obs_data_release(link);
		}
		obs_data_array_release(links);
		s.values.emplace_back("Links", (double)live);
		for (auto &entry : perLink)
			s.values.push_back(std::move(entry));
		result = std::move(s);
	}

	obs_data_release(stream);
	return result;
}

} // namespace

std::string StatsServer::gleemBaseUrl() const
{
	std::string base = url.empty() ? GLEEM_DEFAULT_URL : url;
	while (!base.empty() && base.back() == '/')
		base.pop_back();
	return base;
}

std::optional<StreamStats> StatsServer::fetchGleem(std::string &error, bool &unauthorized) const
{
	if (apiToken.empty()) {
		error = "Gleem API token is not configured";
		return std::nullopt;
	}

	const std::string base = gleemBaseUrl();

	// Without a device uuid the list is enough: it carries every box's full status.
	const bool listAll = publisher.empty();
	const std::string requestUrl = base + (listAll ? "/api/v1/irl/devices" : "/api/v1/irl/devices/" + publisher);

	std::string body;
	long status = 0;
	if (!httpGet(requestUrl, {"Authorization: Bearer " + apiToken, "Accept: application/json"}, body, status,
		     error))
		return std::nullopt;

	if (status == 404) {
		error = "IRL Sidekick " + publisher + " not found";
		return std::nullopt;
	}
	if (status < 200 || status >= 300) {
		error = gleem_api_error(body, status);
		// The token was refused: that says nothing about the stream.
		unauthorized = status == 401 || status == 403;
		return std::nullopt;
	}

	obs_data_t *root = obs_data_create_from_json(body.c_str());
	if (!root) {
		error = "Invalid JSON from the Gleem API";
		return std::nullopt;
	}

	std::optional<StreamStats> result;
	if (listAll) {
		obs_data_array_t *devices = obs_data_get_array(root, "data");
		if (obs_data_array_count(devices) == 0) {
			error = "No IRL Sidekick on this account";
		} else {
			obs_data_t *device = obs_data_array_item(devices, 0);
			result = gleem_device_stats(device, error);
			obs_data_release(device);
		}
		obs_data_array_release(devices);
	} else {
		result = gleem_device_stats(root, error);
	}

	obs_data_release(root);
	return result;
}

bool StatsServer::listGleemDevices(std::vector<GleemDevice> &devices, std::string &error) const
{
	devices.clear();
	error.clear();
	if (apiToken.empty()) {
		error = "Gleem API token is not configured";
		return false;
	}

	std::string body;
	long status = 0;
	if (!httpGet(gleemBaseUrl() + "/api/v1/irl/devices", {"Authorization: Bearer " + apiToken, "Accept: application/json"},
		     body, status, error))
		return false;
	if (status < 200 || status >= 300) {
		error = gleem_api_error(body, status);
		return false;
	}

	obs_data_t *root = obs_data_create_from_json(body.c_str());
	if (!root) {
		error = "Invalid JSON from the Gleem API";
		return false;
	}

	obs_data_array_t *list = obs_data_get_array(root, "data");
	const size_t count = obs_data_array_count(list);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *device = obs_data_array_item(list, i);
		GleemDevice d;
		d.uuid = obs_data_get_string(device, "uuid");
		d.name = obs_data_get_string(device, "name");
		d.online = obs_data_get_bool(device, "online");
		if (!d.uuid.empty())
			devices.push_back(std::move(d));
		obs_data_release(device);
	}
	obs_data_array_release(list);
	obs_data_release(root);
	return true;
}
