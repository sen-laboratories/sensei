/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <sen/SenMapStyles.h>

namespace spatial {

/**
 * The settings of the spatial navigator: lines of `key = value` in `~/config/settings/sen/spatial-navigator.settings` (`#` starts a comment), changed in the settings window of SEN.
 * They say which server answers the questions for what is nearby: the public Overpass API is often busy, and there are mirrors with other limits. Plain C++.
 */

/** A server that answers the Overpass API (the same data, other limits). */
struct Mirror {
	const char*	id;			///< the id in the setting `overpass_servers`
	const char*	name;		///< the name in the list of the settings window
	const char*	info;		///< the description in the list
	const char*	url;		///< the address of the interpreter; empty for the custom one. A `{key}` in it is where the key of the user goes
	bool		requiresKey;	///< the server requires a key; for the custom mirror the setting `overpass_custom_key` says
};

/** The servers of the list, in this order: the default, the mirrors (those without a key first), and `custom` (the last). The list is that of the OpenStreetMap wiki, Overpass API, public instances. */
const std::vector<Mirror>& Mirrors();

struct Settings {
	/** the address of the interpreter of the custom mirror (`https://example.org/api/interpreter`). A `{key}` in it is replaced by the key (for servers that take it in the path). */
	std::string	overpassUrl;
	/** the custom mirror requires a key */
	bool		customRequiresKey = false;
	/** how far around a place what is nearby is looked for, in metres (the area is at least twice this across; a larger place is asked for with its own box) */
	int			poiRadius = 250;
	/** how many things nearby at most: the nearest, the notable ones (a sight) before the shops */
	int			poiLimit = 40;
	/** how long what OpenStreetMap said about a place is kept, in hours: the map is changed all the time, but a place is not remapped that often. It is asked again when the relation is opened later. */
	int			poiCacheHours = 24;
	/** the default style of the map of a place (an id of sen::mapstyles, SenMapStyles.h): the views of the map (Shoji) show it until a style is chosen in the view itself */
	std::string	mapStyle = sen::mapstyles::DefaultId();
	/**
	 * The ids of the servers that the test of the settings found not reachable (`overpass_failed`, ids with blanks between; the settings window writes it, the user does not). The list skips them until
	 * a test of them works. The public server is never in it: it is often only busy.
	 */
	std::vector<std::string>	failedIds;
	/**
	 * The list of the servers (`overpass_servers`, as the list of the services of Basho: `default private-coffee -vk-maps`): the order is the order in which they are asked until one
	 * answers, one with a `-` in front is off and keeps its place. The ids in the order of the file; what it does not name follows in the order of Mirrors(), and is on if it needs no key.
	 */
	std::vector<std::string>	serverOrder;
	/** the ids that the setting names with a `-` */
	std::vector<std::string>	serverOff;
	/** the ids that have a key (`overpass_key_<id>`, the id with _ for -), and the key */
	std::vector<std::pair<std::string, std::string>>	keys;
	/** a file from before the list: the mirror that was chosen (`overpass_mirror`) is the one that is on, and the key (`overpass_key`) is its key. The list in the file replaces both. */
	std::string	legacyMirror, legacyKey;
	/** the file has the list */
	bool		serversNamed = false;
};

/** Reads `key = value` lines into the settings; unknown keys and bad values are ignored (what is there stays). */
void ParseSettings(const std::string& text, Settings* settings);

/** Whether a value can be the value of a setting, so that the settings window can say what is wrong; `reason` gets it. */
bool ValidateSetting(const std::string& key, const std::string& value, std::string* reason = nullptr);

/** What the user is told when none of the servers of the list can be asked. */
extern const char* const kMirrorNotWorking;

/** The ids of the list in the order of the setting (every server once; those that the setting does not name last, in the order of Mirrors()). */
std::vector<std::string> ServerOrder(const Settings& settings);

/** Whether a server (its id) is on: as the setting says; a server that the setting does not name is on if it needs no key. */
bool ServerIsOn(const Settings& settings, const std::string& id);

/** The value of the setting for an order and the ids that are on: `default -private-coffee`. */
std::string ServersText(const std::vector<std::string>& order, const std::vector<std::string>& on);

/** The name of the setting with the key of a server: `overpass_key_nextgis` (`overpass_key_overpass_ch`). Empty if the id is none of the list. */
std::string KeySettingName(const std::string& id);

/** The key of a server (empty if there is none). */
std::string KeyOf(const Settings& settings, const std::string& id);

/** A server that is asked. */
struct Server {
	std::string	id;
	std::string	name;
	std::string	endpoint;	///< with `{key}` still in it
	std::string	key;		///< empty if the server needs none
	bool		isPublic = false;	///< the default server: it is asked whatever happened to it
};

/** Whether a server needs a key: one that is made for it, the custom mirror when the flag says so or its address has a place for the key. */
bool ServerNeedsKey(const Settings& settings, const std::string& id);

/**
 * A server of the list as it would be asked (its address, and its key if it needs one), whether it is on or not. False if it is not complete, and @p problem says what is missing (`Needs an API key`,
 * `Needs an address`).
 */
bool MakeServer(const Settings& settings, const std::string& id, Server* server, std::string* problem = nullptr);

/**
 * The servers that are asked, in the order of the list: those that are on and complete (a custom mirror has an address, one that needs a key has it) and not found unreachable by a test. When no
 * server is on at all it is the default one; when some are on and none is complete, there is none and the user is told.
 */
std::vector<Server> ActiveServers(const Settings& settings);

/** The name of an HTTP status in a word or two: 200 OK, 403 Forbidden, 503 Service Unavailable; `HTTP 418` for one that is not known. */
std::string HttpStatusText(int code);

/**
 * What the test of an Overpass server found, for the line next to the button of the settings: a short text (the HTTP status: `200 OK`, `403 Forbidden`; `No answer` if there was none),
 * the state for the icon (`info` it works, `warning` the server answered with something else than success, `error` it is down: a 5xx status, or not reachable at all) and the detail
 * of several short lines for the tool tip.
 */
struct ServerTest {
	std::string	text;
	std::string	state;
	std::string	detail;
};

/**
 * Judges the answer of a server to the test question. @p httpStatus is 0 if no HTTP answer came (then @p timedOut or @p networkError say why); @p overpass whether the body of a 2xx answer is
 * that of an Overpass server (with @p places things in it); @p extra is more for the detail (a hint about the address, a remark about the key).
 */
ServerTest JudgeServerTest(const std::string& host, int httpStatus, bool timedOut, const std::string& networkError, bool overpass, size_t places, double seconds, int timeoutSeconds,
	const std::string& extra);

/** The ranges of the numbers of the settings. */
const int kMinRadius = 50, kMaxRadius = 1500, kMinLimit = 5, kMaxLimit = 200, kMinCacheHours = 1, kMaxCacheHours = 168;

/** The address of the default server. */
extern const char* const kDefaultOverpassUrl;

}	// namespace spatial
