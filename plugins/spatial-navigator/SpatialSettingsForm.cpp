/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialSettingsForm.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <OS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include <sen/SenMessagePath.h>
#include <sen/SenSettingsForm.h>

#include "../../common/SettingsText.h"
#include "../../common/SocketHttp.h"
#include "SpatialOverpass.h"
#include "SpatialRelations.h"

namespace form = sen::settings::form;
namespace st = sensei::settingstext;

namespace spatial {

namespace {

const char* const kSettingsFile = "sen/spatial-navigator.settings";

// the button that tests the server, and the field next to it that says what came of it (neither is stored)
const char* const kTestButton = "test_overpass";
const char* const kTestResult = "test_result";
const char* const kTestState = "test_state";
const char* const kTestDetail = "test_detail";

// the table of the servers, and the field of the key that shows the cell `key` of the selected row
const char* const kServers = "overpass_servers";
const char* const kKeyField = "overpass_key";
const char* const kTestUserAgent = "SEN-SpatialNavigator/0.1 (Haiku; https://sen-labs.org)";
const bigtime_t kTestTimeout = 20 * 1000000LL;

/** One setting: the key of the file (the name of the field), and what the form shows. */
struct Field {
	const char*	key;
	const char*	label;
	const char*	help;
};

const Field kFields[] = {
	{"poi_radius", "Search radius (m)",
		"How far around a place nearby things are looked for, in metres."},
	{"poi_limit", "Most things nearby",
		"At most this many things; sights come before shops, then the nearest."},
	{"poi_cache_hours", "Keep answers for (hours)",
		"How long an answer is kept before the server is asked again (1 to 168)."},
	{"map_style", "Map style",
		"The default map style in Shoji. A map can choose its own (secondary mouse button)."},
	{"overpass_url", "Custom mirror URL",
		"The interpreter of the custom mirror, like https://host/api/interpreter. {key} marks where its key goes."},
	{"overpass_custom_key", "The custom mirror requires an API key",
		"Tick this if the custom mirror wants a key."},
};

bool
SettingsPath(BPath* path)
{
	return find_directory(B_USER_SETTINGS_DIRECTORY, path, true) == B_OK && path->Append(kSettingsFile) == B_OK;
}


std::string
ReadFileText()
{
	BPath path;
	if (!SettingsPath(&path))
		return std::string();
	BFile file(path.Path(), B_READ_ONLY);
	off_t size = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size <= 0 || size > 64 * 1024)
		return std::string();
	std::string text(size, '\0');
	return file.Read(&text[0], size) == size ? text : std::string();
}


void
ToMessage(const Settings& s, BMessage* message)
{
	message->MakeEmpty();
	message->AddInt32("poi_radius", s.poiRadius);
	message->AddInt32("poi_limit", s.poiLimit);
	message->AddInt32("poi_cache_hours", s.poiCacheHours);
	message->AddString("map_style", s.mapStyle.c_str());
	message->AddString("overpass_url", s.overpassUrl.c_str());
	message->AddBool("overpass_custom_key", s.customRequiresKey);
	// the list of the servers: a row for each, in the order that they are asked; the key of a server is a field of its row (the field of the key follows the selected row)
	for (const std::string& id : ServerOrder(s)) {
		for (const Mirror& mirror : Mirrors()) {
			if (id != mirror.id)
				continue;
			BMessage row;
			row.AddBool("enabled", ServerIsOn(s, id));
			row.AddString("name", mirror.name);
			row.AddString("info", mirror.info);
			row.AddString("id", mirror.id);
			row.AddString("key", KeyOf(s, id).c_str());
			message->AddMessage(kServers, &row);
		}
	}
	message->AddString(kKeyField, "");		// (shows the key of the selected row: never stored)
}

/** The cell of the table that a path key names: `overpass_servers_03.enabled` (row 3, column `enabled`), or `overpass_servers.enabled` when there is a single row. */
bool
ParseCellKey(const std::string& name, const char* table, int32* row, std::string* column)
{
	const size_t length = strlen(table);
	if (name.compare(0, length, table) != 0)
		return false;
	size_t at = length;
	*row = 0;
	if (at < name.size() && name[at] == '_') {
		at++;
		const size_t digits = name.find('.', at);
		if (digits == std::string::npos || digits == at || digits - at > 9)
			return false;
		for (size_t i = at; i < digits; i++) {
			if (name[i] < '0' || name[i] > '9')
				return false;
		}
		*row = (int32)strtol(name.substr(at, digits - at).c_str(), NULL, 10);
		at = digits;
	}
	if (at >= name.size() || name[at] != '.' || at + 1 >= name.size())
		return false;
	*column = name.substr(at + 1);
	return true;
}

/** the host of an address, for the messages (not the whole address: it may have the key in it) */
std::string
HostOf(const std::string& address)
{
	size_t start = address.find("://");
	start = start == std::string::npos ? 0 : start + 3;
	const size_t end = address.find_first_of("/?:", start);
	return address.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}	// namespace


ServerTest
TestOverpassServer(const Settings& settings)
{
	// the first server of the list that is on and complete (also one that an earlier test found not working: it is asked again)
	const std::vector<Server> servers = ActiveServers(settings, true);
	if (servers.empty()) {
		ServerTest nothing;
		nothing.text = "Nothing to test";
		nothing.state = "warning";
		nothing.detail = "No server of the list is on and complete.\nA custom mirror needs its address, a server with a key needs the key.";
		return nothing;
	}
	const Server& first = servers[0];
	const std::string chosen = first.endpoint;
	const std::string host = HostOf(chosen);
	const std::string key = first.key;
	const std::string endpoint = WithKey(chosen, key);
	std::string authorization;
	std::string extra = "Tested: " + first.name + (servers.size() > 1 ? ", the first of the list. The others follow it when it does not answer." : ".");
	if (!key.empty() && chosen.find("{key}") == std::string::npos) {
		if (endpoint.compare(0, 8, "https://") == 0)
			authorization = "Bearer " + key;
		else
			extra += "\nThe key is not sent: the address is not https.";
	}
	// an address that is only a host: the interpreter of an Overpass server is at a path of its own, mostly /api/interpreter
	{
		const size_t scheme = chosen.find("://");
		const size_t slash = chosen.find('/', scheme == std::string::npos ? 0 : scheme + 3);
		if (first.id == "custom" && (slash == std::string::npos || slash + 1 >= chosen.size()))
			extra += "\nThe address has no path: the interpreter is usually at .../api/interpreter.";
	}

	// the same kind of question as the navigator asks, for a small box in the middle of Wien
	Box box;
	box.south = 48.2080; box.north = 48.2090; box.west = 16.3730; box.east = 16.3745;
	box.valid = true;
	const std::string request = PoisUrl(box, endpoint);

	std::string body;
	int status = 0;
	const bigtime_t started = system_time();
	const status_t result = sensei::SocketGet(request, kTestUserAgent, "application/json", &body, &status, kTestTimeout, authorization);
	const double seconds = (system_time() - started) / 1000000.0;

	bool overpass = false;
	size_t places = 0;
	if (result == B_OK) {
		std::vector<Poi> found;
		overpass = ParsePois(body, "", &found);
		places = found.size();
		if (status == 0)
			status = 200;
	}
	const bool timedOut = result == B_TIMED_OUT || result == B_WOULD_BLOCK;	// (a socket that waited too long says "would block")
	return JudgeServerTest(host, status, timedOut, status == 0 && !timedOut ? strerror(result) : std::string(), overpass, places, seconds, (int) (kTestTimeout / 1000000), extra);
}


namespace {

/** puts the keys into the settings file (the lines of other keys, and the comments, stay) */
status_t
WriteKeys(const std::vector<std::pair<std::string, std::string>>& keys)
{
	BPath path;
	if (!SettingsPath(&path))
		return B_ERROR;
	BPath folder;
	if (path.GetParent(&folder) == B_OK)
		create_directory(folder.Path(), 0755);

	const std::string text = st::Update(ReadFileText(), keys);
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	status_t result = file.InitCheck();
	if (result == B_OK && file.Write(text.data(), text.size()) != (ssize_t)text.size())
		result = B_IO_ERROR;
	return result;
}


/**
 * A mirror that fails the test is switched off: the navigator does not ask it (and wait for it) and goes on with the next of the list, and tells the user to look at the settings if there is
 * none, until a test works (or the address is another). The public server is only tested, never switched off: it is often just busy.
 */
void
RememberVerdict(const Settings& tested, ServerTest* test)
{
	const std::vector<Server> servers = ActiveServers(tested, true);
	if (servers.empty() || servers[0].isPublic)
		return;
	const bool works = test->state == "info";
	WriteKeys({{"overpass_failed", works ? std::string() : servers[0].endpoint}});
	if (!works)
		test->detail += "\nThis mirror is not used until a test works.";
}

}	// namespace


void
LoadSettings(Settings* settings)
{
	ParseSettings(ReadFileText(), settings);
}


void
SettingsProvider::Describe(BMessage* settings, BMessage* schema, BMessage* defaults)
{
	Settings current;
	LoadSettings(&current);
	ToMessage(current, settings);
	ToMessage(Settings(), defaults);
	settings->AddBool(kTestButton, false);		// (a button: never stored)
	settings->AddString(kTestResult, fTest.text.empty() ? "Not tested yet." : fTest.text.c_str());
	settings->AddString(kTestState, fTest.state.c_str());
	settings->AddString(kTestDetail, fTest.detail.c_str());

	schema->MakeEmpty();
	schema->AddInt32(form::kSchemaVersion, 1);
	int32 order = 0;
	for (const Field& field : kFields) {
		if (std::string(field.key) == "overpass_url") {
			// the servers: a switch, the name and the description of each, in the order that they are asked (as the services of the location server); under it the key of the row
			// that is selected, which has no field of its own in the table, and then what the custom mirror needs
			BMessage servers;
			servers.AddString(form::kAttrLabel, "Overpass servers");
			servers.AddString(form::kAttrHelp, "Asked in this order until one answers. Drag a row to move it. A server that needs a key is skipped until it has one.");
			servers.AddString(form::kAttrKind, form::kKindTable);
			servers.AddBool(form::kAttrReorderable, true);
			servers.AddInt32(form::kAttrOrder, order++);
			schema->AddMessage(kServers, &servers);
			const struct { const char* column; const char* label; bool readOnly; bool hidden; } columns[] = {
				{"enabled", "Use", false, false}, {"name", "Server", true, false}, {"info", "Description", true, false}, {"id", "", true, true}, {"key", "", true, true},
			};
			int32 columnOrder = 0;
			for (const auto& column : columns) {
				BMessage attributes;
				if (column.label[0] != '\0')
					attributes.AddString(form::kAttrLabel, column.label);
				if (column.readOnly)
					attributes.AddBool(form::kAttrReadOnly, true);
				if (column.hidden)
					attributes.AddBool(form::kAttrHidden, true);
				attributes.AddInt32(form::kAttrOrder, columnOrder++);
				schema->AddMessage((std::string(kServers) + "." + column.column).c_str(), &attributes);
			}
			BMessage keyField;
			keyField.AddString(form::kAttrLabel, "API key");
			keyField.AddString(form::kAttrHelp, "The key of the server that is selected in the list, if it needs one (NextGIS, Overpass.ch, a custom mirror that asks for one). Optional for the others.");
			keyField.AddString(form::kAttrKind, form::kKindSecret);
			keyField.AddString(form::kAttrFollowsTable, kServers);
			keyField.AddString(form::kAttrFollowsColumn, "key");
			keyField.AddInt32(form::kAttrOrder, order++);
			schema->AddMessage(kKeyField, &keyField);
		}
		BMessage attributes;
		attributes.AddString(form::kAttrLabel, field.label);
		attributes.AddString(form::kAttrHelp, field.help);
		attributes.AddInt32(form::kAttrOrder, order++);
		const std::string key = field.key;
		if (key == "poi_radius" || key == "poi_limit" || key == "poi_cache_hours") {
			const bool radius = key == "poi_radius", hours = key == "poi_cache_hours";
			attributes.AddString(form::kAttrKind, form::kKindSpinner);
			attributes.AddDouble(form::kAttrMin, radius ? kMinRadius : hours ? kMinCacheHours : kMinLimit);
			attributes.AddDouble(form::kAttrMax, radius ? kMaxRadius : hours ? kMaxCacheHours : kMaxLimit);
			attributes.AddDouble(form::kAttrStep, radius ? 50 : hours ? 1 : 5);
		} else if (key == "map_style") {
			attributes.AddString(form::kAttrKind, form::kKindChoice);
			for (const sen::mapstyles::Style& style : sen::mapstyles::All()) {
				attributes.AddString(form::kAttrChoices, style.id);
				attributes.AddString(form::kAttrChoiceLabels, style.label);
			}
		}
		schema->AddMessage(field.key, &attributes);
	}

	// a button under the settings of the server: does it work? (the answer is shown next to it)
	BMessage test;
	test.AddString(form::kAttrLabel, "Test the server");
	test.AddString(form::kAttrKind, form::kKindButton);
	test.AddString(form::kAttrShows, kTestResult);
	test.AddString(form::kAttrHelp, "Asks the chosen server a small question and shows the answer.");
	test.AddInt32(form::kAttrOrder, order++);
	schema->AddMessage(kTestButton, &test);
	// the status of the test: the HTTP status as a short text, with the icon of its state in front (info: it works, warning: another status than success, error: down or not reachable) and the
	// detail as the tool tip; the fields of the state and of the detail are not shown
	BMessage result;
	result.AddString(form::kAttrLabel, "Test result");
	result.AddString(form::kAttrHelp, "What the last test found.");
	result.AddBool(form::kAttrReadOnly, true);
	result.AddString(form::kAttrStateField, kTestState);
	result.AddString(form::kAttrDetailField, kTestDetail);
	result.AddInt32(form::kAttrOrder, order++);
	schema->AddMessage(kTestResult, &result);
	for (const char* hidden : {kTestState, kTestDetail}) {
		BMessage invisible;
		invisible.AddBool(form::kAttrHidden, true);
		invisible.AddInt32(form::kAttrOrder, order++);
		schema->AddMessage(hidden, &invisible);
	}
}


status_t
SettingsProvider::Apply(const BMessage& changes, std::vector<std::string>* refused)
{
	std::vector<std::pair<std::string, std::string>> valid;
	status_t first = B_OK;

	// the list of the servers as it is now; changed by a cell or by the whole table, and written once (the order, and the ones that are on)
	Settings current;
	LoadSettings(&current);
	std::vector<std::string> order = ServerOrder(current);
	std::vector<std::string> on;
	for (const std::string& id : order) {
		if (ServerIsOn(current, id))
			on.push_back(id);
	}
	std::vector<std::pair<std::string, std::string>> keys;
	bool listChanged = false;

	char* name = nullptr;
	for (int32 i = 0; changes.GetInfo(B_ANY_TYPE, i, &name, nullptr, nullptr) == B_OK; i++) {
		const std::string key = name;
		if (key == kTestButton) {
			// (the button: the first server of the list, as the settings are in the file now; the answer is shown next to it)
			fTest = TestOverpassServer(current);
			RememberVerdict(current, &fTest);
			continue;
		}
		if (key == kTestResult || key == kTestState || key == kTestDetail || key == kKeyField)
			continue;
		if (key.compare(0, strlen(kServers), kServers) == 0) {
			// the list of the servers: the whole table (Revert, Defaults, a row moved), or a cell of a row (`overpass_servers_02.enabled`, `.key`); the row is the place in the list as
			// it was described
			type_code type = 0;
			int32 count = 0;
			changes.GetInfo(name, &type, &count);
			if (key == kServers && type == B_MESSAGE_TYPE) {
				order.clear();
				on.clear();
				for (int32 v = 0; v < count; v++) {
					BMessage row;
					const std::string id = changes.FindMessage(name, v, &row) == B_OK ? row.GetString("id", "") : std::string();
					if (KeySettingName(id).empty() || std::find(order.begin(), order.end(), id) != order.end()) {
						refused->push_back(key);
						if (first == B_OK)
							first = B_BAD_VALUE;
						continue;
					}
					order.push_back(id);
					if (row.GetBool("enabled", false))
						on.push_back(id);
					const char* newKey = NULL;
					if (row.FindString("key", &newKey) == B_OK && newKey != NULL)
						keys.push_back({KeySettingName(id), st::Trim(newKey)});
				}
				// (what the table did not name keeps its place after the others)
				for (const std::string& id : ServerOrder(current)) {
					if (std::find(order.begin(), order.end(), id) == order.end()) {
						order.push_back(id);
						if (ServerIsOn(current, id))
							on.push_back(id);
					}
				}
				listChanged = true;
				continue;
			}
			int32 row = 0;
			std::string column;
			const std::vector<std::string> before = ServerOrder(current);
			if (!ParseCellKey(key, kServers, &row, &column) || row < 0 || row >= (int32)before.size()) {
				refused->push_back(key);
				if (first == B_OK)
					first = B_NAME_NOT_FOUND;
				continue;
			}
			const std::string id = before[row];
			if (column == "enabled") {
				bool want = false;
				if (changes.FindBool(name, &want) != B_OK) {
					refused->push_back(key);
					if (first == B_OK)
						first = B_BAD_VALUE;
					continue;
				}
				on.erase(std::remove(on.begin(), on.end(), id), on.end());
				if (want)
					on.push_back(id);
				listChanged = true;
			} else if (column == "key") {
				// (a cell is a field of the message, named by its path: the text is the string itself)
				std::string newKey, why;
				const char* typed = NULL;
				if (changes.FindString(name, &typed) == B_OK && typed != NULL)
					newKey = typed;
				if (typed == NULL || !ValidateSetting(KeySettingName(id), newKey, &why)) {
					refused->push_back(why.empty() ? key : key + " (" + why + ")");
					if (first == B_OK)
						first = B_BAD_VALUE;
					continue;
				}
				keys.push_back({KeySettingName(id), st::Trim(newKey)});
			} else {
				refused->push_back(key);
				if (first == B_OK)
					first = B_NAME_NOT_FOUND;
			}
			continue;
		}
		const Field* field = nullptr;
		for (const Field& each : kFields) {
			if (key == each.key)
				field = &each;
		}
		std::string text, why;
		if (field == nullptr || sen::settings::GetText(changes, key, &text) != B_OK || !ValidateSetting(key, text, &why)) {
			refused->push_back(why.empty() ? key : key + " (" + why + ")");
			if (first == B_OK)
				first = field == nullptr ? B_NAME_NOT_FOUND : B_BAD_VALUE;
			continue;
		}
		if (key == "overpass_custom_key")
			text = st::ParseBool(text, false) ? "yes" : "no";
		valid.push_back({key, st::Trim(text)});
	}
	if (listChanged)
		valid.push_back({"overpass_servers", ServersText(order, on)});
	valid.insert(valid.end(), keys.begin(), keys.end());
	if (valid.empty())
		return first;

	const status_t result = WriteKeys(valid);
	return result != B_OK ? result : first;
}

}	// namespace spatial
