/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialSettings.h"

#include "../../../common/SettingsText.h"

namespace spatial {

namespace st = sensei::settingstext;

const char* const kDefaultOverpassUrl = "https://overpass-api.de/api/interpreter";

const std::vector<Mirror>&
Mirrors()
{
	static const std::vector<Mirror> mirrors = {
		{"default", "overpass-api.de (default)", kDefaultOverpassUrl},
		{"private-coffee", "Private.coffee (no limit, no registration)", "https://overpass.private.coffee/api/interpreter"},
		{"vk-maps", "VK Maps (no limit, no registration)", "https://maps.mail.ru/osm/tools/overpass/api/interpreter"},
		{"custom", "Custom mirror", ""},
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
	if (key == "overpass_key")
		return v.find_first_of(" \t") == std::string::npos ? true : fail("a key has no blank");
	return fail("no such setting");
}


std::string
OverpassEndpoint(const Settings& settings)
{
	if (settings.overpassMirror == "custom")
		return settings.overpassUrl.empty() ? std::string(kDefaultOverpassUrl) : settings.overpassUrl;
	for (const Mirror& mirror : Mirrors()) {
		if (settings.overpassMirror == mirror.id && mirror.url[0] != '\0')
			return mirror.url;
	}
	return kDefaultOverpassUrl;
}


std::string
OverpassKey(const Settings& settings)
{
	return settings.overpassMirror == "custom" ? settings.overpassKey : std::string();
}

}	// namespace spatial
