/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

namespace spatial {

/**
 * The settings of the spatial navigator: lines of `key = value` in `~/config/settings/sen/spatial-navigator.settings` (`#` starts a comment), changed in the settings window of SEN.
 * They say which server answers the questions for the points of interest: the public Overpass API is often busy, and there are mirrors with other limits. Plain C++.
 */

/** A server that answers the Overpass API (the same data, other limits). */
struct Mirror {
	const char*	id;			///< the value of the setting `overpass_mirror`
	const char*	label;		///< what the settings window shows
	const char*	url;		///< the address of the interpreter; empty for the custom one
};

/** The choices of the settings window, in this order: the default, the mirrors, and `custom` (the last). */
const std::vector<Mirror>& Mirrors();

struct Settings {
	/** the id of a mirror (`default`, `private-coffee`, `vk-maps`) or `custom` */
	std::string	overpassMirror = "default";
	/** the address of the interpreter of a custom mirror (`https://example.org/api/interpreter`). A `{key}` in it is replaced by the key (for servers that take it in the path). */
	std::string	overpassUrl;
	/** the key of a custom mirror that wants one (optional). Sent as `Authorization: Bearer <key>` (only over https), or in place of `{key}` in the address. */
	std::string	overpassKey;
};

/** Reads `key = value` lines into the settings; unknown keys and bad values are ignored (what is there stays). */
void ParseSettings(const std::string& text, Settings* settings);

/** Whether a value can be the value of a setting, so that the settings window can say what is wrong; `reason` gets it. */
bool ValidateSetting(const std::string& key, const std::string& value, std::string* reason = nullptr);

/** The address of the interpreter that is asked: the mirror that is chosen, else the default (a custom mirror with no address is the default, too). */
std::string OverpassEndpoint(const Settings& settings);

/** The key that goes with that address: only a custom mirror has one; empty if there is none. */
std::string OverpassKey(const Settings& settings);

/** The address of the default server. */
extern const char* const kDefaultOverpassUrl;

}	// namespace spatial
