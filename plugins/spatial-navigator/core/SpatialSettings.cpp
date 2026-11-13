/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialSettings.h"

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
		{"default", "overpass-api.de (default)", kDefaultOverpassUrl, false},
		{"private-coffee", "Private.coffee (no limit, no registration)", "https://overpass.private.coffee/api/interpreter", false},
		{"vk-maps", "VK Maps (no limit, no registration)", "https://maps.mail.ru/osm/tools/overpass/api/interpreter", false},
		{"nextgis", "NextGIS (free, API key)", "https://overpass.nextgis.com/{key}/api/interpreter", true},
		{"overpass-ch", "Overpass.ch (free tier, API key)", "https://api.overpass.ch/v1/{key}/interpreter", true},
		{"custom", "Custom mirror", "", false},
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

}	// namespace


void
ParseSettings(const std::string& text, Settings* settings)
{
	for (const auto& pair : st::Pairs(text)) {
		const std::string& key = pair.first;
		const std::string value = st::Trim(pair.second);
		if (key == "overpass_mirror") {
			if (IsMirrorId(value))
				settings->overpassMirror = value;
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
			settings->overpassKey = value;
		}
	}
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
	if (key == "overpass_mirror")
		return IsMirrorId(v) ? true : fail("one of the mirrors of the list");
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
	if (key == "overpass_key")
		return v.find_first_of(" \t") == std::string::npos ? true : fail("a key has no blank");
	return fail("no such setting");
}


std::string
OverpassKey(const Settings& settings)
{
	for (const Mirror& mirror : Mirrors()) {
		if (settings.overpassMirror == mirror.id) {
			const bool required = mirror.requiresKey || (settings.overpassMirror == "custom" && settings.customRequiresKey);
			return required ? settings.overpassKey : std::string();
		}
	}
	return std::string();
}


std::string
OverpassEndpoint(const Settings& settings)
{
	std::string url = kDefaultOverpassUrl;
	if (settings.overpassMirror == "custom") {
		if (!settings.overpassUrl.empty())
			url = settings.overpassUrl;
	} else {
		for (const Mirror& mirror : Mirrors()) {
			if (settings.overpassMirror == mirror.id && mirror.url[0] != '\0')
				url = mirror.url;
		}
	}
	// (an address with a place for the key, and no key: the public server answers)
	if (url.find("{key}") != std::string::npos && OverpassKey(settings).empty())
		return kDefaultOverpassUrl;
	return url;
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
			test.state = "info";
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


std::string
KeyEnabledWhen()
{
	std::string when;
	for (const Mirror& mirror : Mirrors()) {
		if (mirror.requiresKey)
			when += (when.empty() ? "" : "|") + std::string("overpass_mirror=") + mirror.id;
	}
	return when + (when.empty() ? "" : "|") + "overpass_mirror=custom&overpass_custom_key=true";
}

}	// namespace spatial
