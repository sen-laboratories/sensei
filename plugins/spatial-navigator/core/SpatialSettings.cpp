/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialSettings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "../../../common/SettingsText.h"

namespace spatial {

namespace st = sensei::settingstext;

const char* const kDefaultOverpassUrl = "https://overpass-api.de/api/interpreter";

const std::vector<Mirror>&
Mirrors()
{
	static const std::vector<Mirror> mirrors = {
		{"default", "overpass-api.de", "The public server of the OpenStreetMap community: often busy.", kDefaultOverpassUrl, false},
		{"private-coffee", "Private.coffee", "A public mirror, no limit, no registration.", "https://overpass.private.coffee/api/interpreter", false},
		{"vk-maps", "VK Maps", "A public mirror, no limit, no registration.", "https://maps.mail.ru/osm/tools/overpass/api/interpreter", false},
		{"nextgis", "NextGIS", "Free with an API key (nextgis.com).", "https://overpass.nextgis.com/{key}/api/interpreter", true},
		{"overpass-ch", "Overpass.ch", "Free tier with an API key (overpass.ch).", "https://api.overpass.ch/v1/{key}/interpreter", true},
		{"custom", "Custom mirror", "Your own server: enter its address below.", "", false},
	};
	return mirrors;
}


namespace {

bool
IsMirrorId(const std::string& id)
{
	for (const Mirror& mirror : Mirrors()) {
		if (id == mirror.id)
			return true;
	}
	return false;
}

/** a whole number in a range (as the settings window sends it: digits) */
bool
ParseNumber(const std::string& v, int low, int high, int* number)
{
	if (v.empty() || v.size() > 6 || v.find_first_not_of("0123456789") != std::string::npos)
		return false;
	const int value = std::atoi(v.c_str());
	if (value < low || value > high)
		return false;
	*number = value;
	return true;
}

/** `https://host/path` or `http://host/path`, with no blank */
bool
IsAddress(const std::string& v)
{
	const bool https = v.compare(0, 8, "https://") == 0;
	if (!https && v.compare(0, 7, "http://") != 0)
		return false;
	if (v.size() <= (https ? 8u : 7u) || v.find_first_of(" \t\n\r") != std::string::npos)
		return false;
	return true;
}

/** `private-coffee` or `-private-coffee` (off): the id, if it is a server of the list */
bool
ServerToken(const std::string& token, std::string* id, bool* off)
{
	*off = !token.empty() && token[0] == '-';
	*id = *off ? token.substr(1) : token;
	return IsMirrorId(*id);
}

void
ParseServers(const std::string& value, Settings* settings)
{
	settings->serverOrder.clear();
	settings->serverOff.clear();
	settings->serversNamed = false;
	size_t at = 0;
	while (at < value.size()) {
		const size_t end = value.find_first_of(" \t", at);
		const std::string token = value.substr(at, end == std::string::npos ? std::string::npos : end - at);
		at = end == std::string::npos ? value.size() : end + 1;
		std::string id;
		bool off = false;
		if (token.empty() || !ServerToken(token, &id, &off))
			continue;		// (a name that is none of the list is left out: a typo must not switch the others off)
		if (std::find(settings->serverOrder.begin(), settings->serverOrder.end(), id) != settings->serverOrder.end())
			continue;
		settings->serverOrder.push_back(id);
		if (off)
			settings->serverOff.push_back(id);
		settings->serversNamed = true;
	}
}

/** `private-coffee vk-maps`: the ids that are servers of the list and not the public one */
void
ParseFailed(const std::string& value, Settings* settings)
{
	settings->failedIds.clear();
	size_t at = 0;
	while (at < value.size()) {
		const size_t end = value.find_first_of(" \t", at);
		const std::string token = value.substr(at, end == std::string::npos ? std::string::npos : end - at);
		at = end == std::string::npos ? value.size() : end + 1;
		if (!token.empty() && token != "default" && IsMirrorId(token) && std::find(settings->failedIds.begin(), settings->failedIds.end(), token) == settings->failedIds.end())
			settings->failedIds.push_back(token);
	}
}

void
SetKey(Settings* settings, const std::string& id, const std::string& key)
{
	for (auto it = settings->keys.begin(); it != settings->keys.end(); ++it) {
		if (it->first == id) {
			if (key.empty())
				settings->keys.erase(it);
			else
				it->second = key;
			return;
		}
	}
	if (!key.empty())
		settings->keys.push_back({id, key});
}

/** the id of the server that a name of a setting `overpass_key_<id>` is the key of; empty if it is none */
std::string
KeyOwner(const std::string& name)
{
	for (const Mirror& mirror : Mirrors()) {
		if (!KeySettingName(mirror.id).empty() && name == KeySettingName(mirror.id))
			return mirror.id;
	}
	return std::string();
}

void
SetAddress(Settings* settings, const std::string& id, const std::string& address)
{
	for (auto it = settings->addresses.begin(); it != settings->addresses.end(); ++it) {
		if (it->first == id) {
			if (address.empty())
				settings->addresses.erase(it);
			else
				it->second = address;
			return;
		}
	}
	if (!address.empty())
		settings->addresses.push_back({id, address});
}

void
SetNeedsKeyFlag(Settings* settings, const std::string& id, bool flag)
{
	for (auto& entry : settings->needsKeyFlags) {
		if (entry.first == id) {
			entry.second = flag;
			return;
		}
	}
	settings->needsKeyFlags.push_back({id, flag});
}

/** the id of the server that a setting is the address (or the flag) of, for the names that are not those of the custom mirror; empty if it is none */
std::string
AddressOwner(const std::string& name)
{
	for (const Mirror& mirror : Mirrors()) {
		if (std::string(mirror.id) != "custom" && name == AddressSettingName(mirror.id))
			return mirror.id;
	}
	return std::string();
}

std::string
NeedsKeyOwner(const std::string& name)
{
	for (const Mirror& mirror : Mirrors()) {
		if (std::string(mirror.id) != "custom" && name == NeedsKeySettingName(mirror.id))
			return mirror.id;
	}
	return std::string();
}

/** a file from before the list: the mirror that was chosen is the only one that is on, with the key that there was */
void
ApplyLegacy(Settings* settings)
{
	if (settings->serversNamed || !IsMirrorId(settings->legacyMirror))
		return;
	settings->serverOrder.assign(1, settings->legacyMirror);
	settings->serverOff.clear();
	for (const Mirror& mirror : Mirrors()) {
		if (settings->legacyMirror != mirror.id) {
			settings->serverOrder.push_back(mirror.id);
			settings->serverOff.push_back(mirror.id);
		}
	}
	if (!settings->legacyKey.empty() && KeyOf(*settings, settings->legacyMirror).empty())
		SetKey(settings, settings->legacyMirror, settings->legacyKey);
}

}	// namespace


void
ParseSettings(const std::string& text, Settings* settings)
{
	for (const auto& pair : st::Pairs(text)) {
		const std::string& key = pair.first;
		const std::string value = st::Trim(pair.second);
		if (key == "overpass_servers") {
			ParseServers(value, settings);
		} else if (key == "overpass_mirror") {
			// (from before the list)
			if (IsMirrorId(value))
				settings->legacyMirror = value;
		} else if (key == "overpass_url") {
			if (value.empty() || IsAddress(value))
				settings->overpassUrl = value;
		} else if (key == "poi_radius") {
			ParseNumber(value, kMinRadius, kMaxRadius, &settings->poiRadius);
		} else if (key == "poi_limit") {
			ParseNumber(value, kMinLimit, kMaxLimit, &settings->poiLimit);
		} else if (key == "map_style") {
			if (sen::mapstyles::Find(value) != nullptr)
				settings->mapStyle = value;
		} else if (key == "poi_cache_hours") {
			ParseNumber(value, kMinCacheHours, kMaxCacheHours, &settings->poiCacheHours);
		} else if (key == "overpass_custom_key") {
			settings->customRequiresKey = st::ParseBool(value, settings->customRequiresKey);
		} else if (key == "overpass_key") {
			settings->legacyKey = value;
		} else if (key == "overpass_failed") {
			ParseFailed(value, settings);
		} else if (!KeyOwner(key).empty()) {
			SetKey(settings, KeyOwner(key), value);
		} else if (!AddressOwner(key).empty()) {
			if (value.empty() || IsAddress(value))
				SetAddress(settings, AddressOwner(key), value);		// (empty: the address of the list)
		} else if (!NeedsKeyOwner(key).empty()) {
			if (st::IsBool(value))
				SetNeedsKeyFlag(settings, NeedsKeyOwner(key), st::ParseBool(value, false));
		}
	}
	ApplyLegacy(settings);
}


bool
ValidateSetting(const std::string& key, const std::string& value, std::string* reason)
{
	auto fail = [reason](const char* text) {
		if (reason != nullptr)
			*reason = text;
		return false;
	};

	const std::string v = st::Trim(value);
	if (v.find_first_of("#\n\r") != std::string::npos)
		return fail("a # or a line break cannot be stored in the settings file");
	if (key == "overpass_servers") {
		size_t at = 0;
		while (at < v.size()) {
			const size_t end = v.find_first_of(" \t", at);
			const std::string token = v.substr(at, end == std::string::npos ? std::string::npos : end - at);
			at = end == std::string::npos ? v.size() : end + 1;
			std::string id;
			bool off = false;
			if (!token.empty() && !ServerToken(token, &id, &off))
				return fail("servers of the list, with a - in front of those that are off");
		}
		return true;
	}
	if (key == "overpass_url")
		return v.empty() || IsAddress(v) ? true : fail("an address like https://example.org/api/interpreter");
	int number = 0;
	if (key == "poi_radius")
		return ParseNumber(v, kMinRadius, kMaxRadius, &number) ? true : fail("a number of metres from 50 to 1500");
	if (key == "poi_limit")
		return ParseNumber(v, kMinLimit, kMaxLimit, &number) ? true : fail("a number from 5 to 200");
	if (key == "map_style")
		return sen::mapstyles::Find(v) != nullptr ? true : fail("one of the styles of the list");
	if (key == "poi_cache_hours")
		return ParseNumber(v, kMinCacheHours, kMaxCacheHours, &number) ? true : fail("a number of hours from 1 to 168");
	if (key == "overpass_custom_key")
		return st::IsBool(v) ? true : fail("yes or no");
	if (!AddressOwner(key).empty())
		return v.empty() || IsAddress(v) ? true : fail("an address like https://example.org/api/interpreter");
	if (!NeedsKeyOwner(key).empty())
		return v.empty() || st::IsBool(v) ? true : fail("yes or no");
	if (!KeyOwner(key).empty())
		return v.find_first_of(" \t") == std::string::npos ? true : fail("a key has no blank");
	return fail("no such setting");
}


std::string
KeySettingName(const std::string& id)
{
	if (!IsMirrorId(id))
		return std::string();
	std::string name = id;
	std::replace(name.begin(), name.end(), '-', '_');
	return "overpass_key_" + name;
}


std::string
AddressSettingName(const std::string& id)
{
	if (!IsMirrorId(id))
		return std::string();
	if (id == "custom")
		return "overpass_url";
	std::string name = id;
	std::replace(name.begin(), name.end(), '-', '_');
	return "overpass_url_" + name;
}


std::string
NeedsKeySettingName(const std::string& id)
{
	if (!IsMirrorId(id))
		return std::string();
	if (id == "custom")
		return "overpass_custom_key";
	std::string name = id;
	std::replace(name.begin(), name.end(), '-', '_');
	return "overpass_needs_key_" + name;
}


std::string
DefaultAddress(const std::string& id)
{
	for (const Mirror& mirror : Mirrors()) {
		if (id == mirror.id)
			return mirror.url;
	}
	return std::string();
}


std::string
AddressOf(const Settings& settings, const std::string& id)
{
	if (id == "custom")
		return settings.overpassUrl;
	for (const auto& entry : settings.addresses) {
		if (entry.first == id)
			return entry.second;
	}
	return DefaultAddress(id);
}


bool
DefaultNeedsKeyFlag(const std::string& id)
{
	for (const Mirror& mirror : Mirrors()) {
		if (id == mirror.id)
			return mirror.requiresKey;
	}
	return false;
}


bool
NeedsKeyFlag(const Settings& settings, const std::string& id)
{
	if (id == "custom")
		return settings.customRequiresKey;
	for (const auto& entry : settings.needsKeyFlags) {
		if (entry.first == id)
			return entry.second;
	}
	return DefaultNeedsKeyFlag(id);
}


std::string
KeyOf(const Settings& settings, const std::string& id)
{
	for (const auto& entry : settings.keys) {
		if (entry.first == id)
			return entry.second;
	}
	return std::string();
}


std::vector<std::string>
ServerOrder(const Settings& settings)
{
	std::vector<std::string> order;
	for (const std::string& id : settings.serverOrder) {
		if (IsMirrorId(id) && std::find(order.begin(), order.end(), id) == order.end())
			order.push_back(id);
	}
	for (const Mirror& mirror : Mirrors()) {
		if (std::find(order.begin(), order.end(), std::string(mirror.id)) == order.end())
			order.push_back(mirror.id);
	}
	return order;
}


bool
ServerIsOn(const Settings& settings, const std::string& id)
{
	if (std::find(settings.serverOrder.begin(), settings.serverOrder.end(), id) != settings.serverOrder.end())
		return std::find(settings.serverOff.begin(), settings.serverOff.end(), id) == settings.serverOff.end();
	for (const Mirror& mirror : Mirrors()) {
		if (id == mirror.id)
			return !mirror.requiresKey && id != "custom";
	}
	return false;
}


std::string
ServersText(const std::vector<std::string>& order, const std::vector<std::string>& on)
{
	std::string text;
	for (const std::string& id : order) {
		if (!text.empty())
			text += ' ';
		if (std::find(on.begin(), on.end(), id) == on.end())
			text += '-';
		text += id;
	}
	return text;
}


const char* const kMirrorNotWorking = "Your configured OSM mirror is not working, please check settings.";


bool
ServerNeedsKey(const Settings& settings, const std::string& id)
{
	if (!IsMirrorId(id))
		return false;
	return NeedsKeyFlag(settings, id) || AddressOf(settings, id).find("{key}") != std::string::npos;
}


bool
MakeServer(const Settings& settings, const std::string& id, Server* server, std::string* problem)
{
	for (const Mirror& mirror : Mirrors()) {
		if (id != mirror.id)
			continue;
		const std::string endpoint = AddressOf(settings, id);
		if (endpoint.empty()) {
			if (problem != nullptr)
				*problem = "Needs an address";
			return false;
		}
		const bool needsKey = ServerNeedsKey(settings, id);
		const std::string key = needsKey ? KeyOf(settings, id) : std::string();
		if (needsKey && key.empty()) {
			if (problem != nullptr)
				*problem = "Needs an API key";
			return false;
		}
		server->id = id;
		server->name = mirror.name;
		server->endpoint = endpoint;
		server->key = key;
		server->isPublic = id == "default";
		return true;
	}
	return false;
}


std::vector<Server>
ActiveServers(const Settings& settings)
{
	std::vector<Server> found;
	bool anyOn = false;
	for (const std::string& id : ServerOrder(settings)) {
		if (!ServerIsOn(settings, id))
			continue;
		anyOn = true;
		Server server;
		if (!MakeServer(settings, id, &server))
			continue;
		if (!server.isPublic && std::find(settings.failedIds.begin(), settings.failedIds.end(), id) != settings.failedIds.end())
			continue;
		found.push_back(server);
	}
	if (!anyOn) {
		Server server;
		MakeServer(settings, "default", &server);
		found.push_back(server);
	}
	return found;
}


std::string
HttpStatusText(int code)
{
	const char* name = NULL;
	switch (code) {
		case 200: name = "OK"; break;
		case 204: name = "No Content"; break;
		case 301: name = "Moved Permanently"; break;
		case 302: name = "Found"; break;
		case 400: name = "Bad Request"; break;
		case 401: name = "Unauthorized"; break;
		case 403: name = "Forbidden"; break;
		case 404: name = "Not Found"; break;
		case 405: name = "Method Not Allowed"; break;
		case 408: name = "Request Timeout"; break;
		case 414: name = "URI Too Long"; break;
		case 429: name = "Too Many Requests"; break;
		case 500: name = "Internal Server Error"; break;
		case 502: name = "Bad Gateway"; break;
		case 503: name = "Service Unavailable"; break;
		case 504: name = "Gateway Timeout"; break;
	}
	return name != NULL ? std::to_string(code) + " " + name : "HTTP " + std::to_string(code);
}


ServerTest
JudgeServerTest(const std::string& host, int httpStatus, bool timedOut, const std::string& networkError, bool overpass, size_t places, double seconds, int timeoutSeconds,
	const std::string& extra)
{
	char took[32];
	snprintf(took, sizeof(took), "%.1f s", seconds);
	ServerTest test;
	std::string explanation;
	if (httpStatus >= 200 && httpStatus < 300) {
		test.text = HttpStatusText(httpStatus);
		if (overpass) {
			test.state = "ok";
			explanation = std::string("Answered in ") + took + " (" + std::to_string(places) + " places in the test area).";
		} else {
			test.state = "warning";
			test.text += ", not Overpass";
			explanation = std::string("Answered in ") + took + ", but not like an Overpass server: check the address.";
		}
	} else if (httpStatus > 0) {
		test.text = HttpStatusText(httpStatus);
		test.state = httpStatus >= 500 ? "error" : "warning";
		explanation = "Status " + HttpStatusText(httpStatus) + " after " + took + ".";
		if (httpStatus >= 500)
			explanation += "\nThe server is overloaded or down. Try again later, or choose another server.";
		else if (httpStatus == 401 || httpStatus == 403)
			explanation += "\nThe key is missing, wrong or not for this server.";
		else if (httpStatus == 404)
			explanation += "\nNo such address: check the address of the mirror.";
		else if (httpStatus == 400)
			explanation += "\nThe server did not understand the question: check the address of the mirror.";
		else if (httpStatus == 429)
			explanation += "\nToo many questions: try again later, or choose another server.";
	} else {
		test.state = "error";
		if (timedOut) {
			test.text = "No answer";
			explanation = "No answer in " + std::to_string(timeoutSeconds) + " s.\nThe server is down, or the address is wrong.";
		} else {
			test.text = "Not reachable";
			explanation = "Could not connect" + (networkError.empty() ? std::string() : ": " + networkError) + ".\nCheck the address and the network.";
		}
	}
	test.detail = host + "\n" + explanation;
	if (!extra.empty())
		test.detail += "\n" + extra;
	return test;
}

}	// namespace spatial
