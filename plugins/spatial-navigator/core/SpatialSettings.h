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
	const char*	url;		///< the address of the interpreter; empty for the custom one. A `{key}` in it is where the key of the user goes
	bool		requiresKey;	///< the server requires a key (the settings window enables the field for the key only then); for the custom mirror the setting `overpass_custom_key` says
};

/** The choices of the settings window, in this order: the default, the mirrors (those without a key first), and `custom` (the last). The list is that of the OpenStreetMap wiki, Overpass API, public instances. */
const std::vector<Mirror>& Mirrors();

struct Settings {
	/** the id of a mirror (`default`, `private-coffee`, `vk-maps`) or `custom` */
	std::string	overpassMirror = "default";
	/** the address of the interpreter of a custom mirror (`https://example.org/api/interpreter`). A `{key}` in it is replaced by the key (for servers that take it in the path). */
	std::string	overpassUrl;
	/** the custom mirror requires a key (the field of the key is enabled only for a server that requires one) */
	bool		customRequiresKey = false;
	/** how far around a place the points of interest are looked for, in metres (the area is at least twice this across; a larger place is asked for with its own box) */
	int			poiRadius = 250;
	/** how many points of interest at most: the nearest, the notable ones (a sight) before the shops */
	int			poiLimit = 40;
	/** the key of a server that requires one. Put in place of `{key}` in the address, else sent as `Authorization: Bearer <key>` (only over https). */
	std::string	overpassKey;
};

/** Reads `key = value` lines into the settings; unknown keys and bad values are ignored (what is there stays). */
void ParseSettings(const std::string& text, Settings* settings);

/** Whether a value can be the value of a setting, so that the settings window can say what is wrong; `reason` gets it. */
bool ValidateSetting(const std::string& key, const std::string& value, std::string* reason = nullptr);

/**
 * The address of the interpreter that is asked (with `{key}` still in it): the mirror that is chosen, else the default: a custom mirror with no address is the default, and so is any
 * address that wants a key (`{key}`) while there is none.
 */
std::string OverpassEndpoint(const Settings& settings);

/** The key that goes with that address: that of a server that requires one (a custom one if the flag says so); empty if there is none. */
std::string OverpassKey(const Settings& settings);

/**
 * When the field of the key is enabled, as the `enabled_when` of the schema: the servers that require a key, or the custom mirror with the flag
 * (`overpass_mirror=nextgis|overpass_mirror=overpass-ch|overpass_mirror=custom&overpass_custom_key=true`; `|` is or, `&` is and).
 */
std::string KeyEnabledWhen();

/** The ranges of the numbers of the settings. */
const int kMinRadius = 50, kMaxRadius = 1500, kMinLimit = 5, kMaxLimit = 200;

/** The address of the default server. */
extern const char* const kDefaultOverpassUrl;

}	// namespace spatial
