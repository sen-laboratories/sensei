/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialSettingsForm.h"

#include <Application.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <OS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <mutex>
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
const char* const kStatusFile = "sen/spatial-navigator/server-status";

// the table of the servers, and the command of the message that it sends when another row is selected: the field of the API key listens to it (SenSettingsForm.h, master and detail)
const char* const kServers = "overpass_servers";
const char* const kKeyField = "overpass_key";
const char* const kRowCommand = "OVrw";
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


// What the last test of a server found, for the column Status: the state (`ok`, `warning`, `error`, or empty), the short text and the detail for the tool tip. It is kept between the sessions in
// the cache folder (a status that is there says when the server was last asked, the test is not repeated when the window is opened), and while a test is under way `testing` is true.
struct Status {
	std::string	state;
	std::string	text;
	std::string	detail;
	bool		testing = false;
};

std::mutex sStatusLock;
std::map<std::string, Status> sStatus;
bool sStatusLoaded = false;
std::mutex sFileLock;

std::string
Escaped(const std::string& text)
{
	std::string out;
	for (char c : text) {
		if (c == '\\')
			out += "\\\\";
		else if (c == '\n')
			out += "\\n";
		else if (c == '\t')
			out += ' ';
		else
			out += c;
	}
	return out;
}


std::string
Unescaped(const std::string& text)
{
	std::string out;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '\\' && i + 1 < text.size()) {
			i++;
			out += text[i] == 'n' ? '\n' : text[i];
		} else {
			out += text[i];
		}
	}
	return out;
}


bool
StatusPath(BPath* path)
{
	return find_directory(B_USER_CACHE_DIRECTORY, path, true) == B_OK && path->Append(kStatusFile) == B_OK;
}


/** (the lock is held) */
void
LoadStatus()
{
	if (sStatusLoaded)
		return;
	sStatusLoaded = true;
	BPath path;
	if (!StatusPath(&path))
		return;
	BFile file(path.Path(), B_READ_ONLY);
	off_t size = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size <= 0 || size > 64 * 1024)
		return;
	std::string text(size, '\0');
	if (file.Read(&text[0], size) != size)
		return;
	size_t at = 0;
	while (at < text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = text.substr(at, end - at);
		at = end + 1;
		std::vector<std::string> parts;
		size_t from = 0;
		for (int field = 0; field < 4; field++) {
			const size_t tab = field < 3 ? line.find('\t', from) : std::string::npos;
			parts.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
			if (tab == std::string::npos)
				break;
			from = tab + 1;
		}
		if (parts.size() == 4) {
			Status status;
			status.state = parts[1];
			status.text = Unescaped(parts[2]);
			status.detail = Unescaped(parts[3]);
			sStatus[parts[0]] = status;
		}
	}
}


/** (the lock is held) */
void
SaveStatus()
{
	BPath path, folder;
	if (!StatusPath(&path) || path.GetParent(&folder) != B_OK)
		return;
	create_directory(folder.Path(), 0755);
	std::string text;
	for (const auto& entry : sStatus) {
		if (entry.second.testing)
			continue;
		text += entry.first + "\t" + entry.second.state + "\t" + Escaped(entry.second.text) + "\t" + Escaped(entry.second.detail) + "\n";
	}
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	if (file.InitCheck() == B_OK)
		file.Write(text.data(), text.size());
}


/** What the column says for a server: what is missing for one that cannot be asked yet, else what the last test found, else that there was none. */
Status
StatusOf(const Settings& settings, const std::string& id)
{
	Server server;
	std::string problem;
	if (!MakeServer(settings, id, &server, &problem)) {
		// (a server that is on and cannot be asked is a warning; one that is off only says what it would need)
		Status missing;
		missing.state = ServerIsOn(settings, id) ? form::kStateWarning : "";
		missing.text = problem;
		missing.detail = problem == "Needs an address" ? "Enter the address of the custom mirror below." : "Select the row and enter the API key below.";
		return missing;
	}
	std::lock_guard<std::mutex> lock(sStatusLock);
	LoadStatus();
	auto found = sStatus.find(id);
	if (found != sStatus.end())
		return found->second;
	Status none;
	none.text = "Not tested";
	none.detail = "The server is tested when it is switched on.";
	return none;
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
	// the list of the servers: a row for each, in the order that they are asked. The key of a server is a message of its row (`overpass_key`, with `value` and `enabled`, whether the server needs
	// one): the field of the key is not told which row it is for, it gets that message when the row is selected
	for (const std::string& id : ServerOrder(s)) {
		for (const Mirror& mirror : Mirrors()) {
			if (id != mirror.id)
				continue;
			const Status status = StatusOf(s, id);
			BMessage row;
			row.AddBool("enabled", ServerIsOn(s, id));
			row.AddString("name", mirror.name);
			row.AddString("info", mirror.info);
			row.AddString("status", status.text.c_str());
			row.AddString("id", mirror.id);
			row.AddString("status_state", status.state.c_str());
			row.AddString("status_detail", status.detail.c_str());
			row.AddBool("testing", status.testing);
			BMessage key;
			key.AddString(form::kUpdateValue, KeyOf(s, id).c_str());
			key.AddBool(form::kUpdateEnabled, ServerNeedsKey(s, id));
			row.AddMessage(kKeyField, &key);
			message->AddMessage(kServers, &row);
		}
	}
	message->AddString(kKeyField, "");		// (the field of the key keeps nothing: what it shows is that of the selected row)
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

/** puts the keys into the settings file (the lines of other keys, and the comments, stay) */
status_t
WriteKeys(const std::vector<std::pair<std::string, std::string>>& keys)
{
	std::lock_guard<std::mutex> lock(sFileLock);
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

}	// namespace


ServerTest
TestOverpassServer(const Server& server)
{
	const std::string host = HostOf(server.endpoint);
	const std::string endpoint = WithKey(server.endpoint, server.key);
	std::string authorization;
	std::string extra = "Tested: " + server.name + ".";
	if (!server.key.empty() && server.endpoint.find("{key}") == std::string::npos) {
		if (endpoint.compare(0, 8, "https://") == 0)
			authorization = "Bearer " + server.key;
		else
			extra += "\nThe key is not sent: the address is not https.";
	}
	// an address that is only a host: the interpreter of an Overpass server is at a path of its own, mostly /api/interpreter
	{
		const size_t scheme = server.endpoint.find("://");
		const size_t slash = server.endpoint.find('/', scheme == std::string::npos ? 0 : scheme + 3);
		if (server.id == "custom" && (slash == std::string::npos || slash + 1 >= server.endpoint.size()))
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

/** What a test found goes to the status of the server, and a mirror (not the public server) that is not reachable is left out of the list until a test of it works. */
void
TestDone(const Server& server, const ServerTest& test)
{
	{
		std::lock_guard<std::mutex> lock(sStatusLock);
		LoadStatus();
		Status status;
		status.state = test.state;
		status.text = test.text;
		status.detail = test.detail;
		sStatus[server.id] = status;
		SaveStatus();
	}
	if (!server.isPublic) {
		Settings current;
		ParseSettings(ReadFileText(), &current);
		std::vector<std::string> failed = current.failedIds;
		failed.erase(std::remove(failed.begin(), failed.end(), server.id), failed.end());
		if (test.state == form::kStateError)
			failed.push_back(server.id);
		std::string text;
		for (const std::string& id : failed)
			text += (text.empty() ? "" : " ") + id;
		WriteKeys({{"overpass_failed", text}});
	}
	// the window shows the new status (the application sends the settings to it)
	if (be_app != NULL)
		be_app_messenger.SendMessage(kPushSettings);
}


int32
RunTest(void* data)
{
	Server* server = (Server*)data;
	const ServerTest test = TestOverpassServer(*server);
	TestDone(*server, test);
	delete server;
	return 0;
}


/** A server is tested in the background (it can take 20 seconds): the status shows that it is under way, and the result is pushed to the window when it is there. */
void
StartTest(const std::string& id)
{
	Settings current;
	ParseSettings(ReadFileText(), &current);
	Server server;
	std::string problem;
	if (!MakeServer(current, id, &server, &problem))
		return;		// (the column says what is missing)
	{
		std::lock_guard<std::mutex> lock(sStatusLock);
		LoadStatus();
		if (sStatus[id].testing)
			return;
		Status status;
		status.text = "Testing...";
		status.detail = "Asking " + server.name + " a small question.";
		status.testing = true;
		sStatus[id] = status;
	}
	const thread_id thread = spawn_thread(RunTest, "overpass test", B_NORMAL_PRIORITY, new Server(server));
	if (thread < B_OK || resume_thread(thread) != B_OK) {
		std::lock_guard<std::mutex> lock(sStatusLock);
		sStatus[id] = Status();
	}
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

	schema->MakeEmpty();
	schema->AddInt32(form::kSchemaVersion, 1);
	int32 order = 0;
	for (const Field& field : kFields) {
		if (std::string(field.key) == "overpass_url") {
			// the servers: a switch, the name, the description and the status of each, in the order that they are asked (as the services of the location server). The table sends a message
			// with the key of the row that is selected, and the field of the key under it listens to that message; then what the custom mirror needs
			BMessage servers;
			servers.AddString(form::kAttrLabel, "Overpass servers");
			servers.AddString(form::kAttrHelp, "Asked in this order until one answers. Drag a row to move it. A server is tested when it is switched on; one that needs a key is skipped until it has one.");
			servers.AddString(form::kAttrKind, form::kKindTable);
			servers.AddBool(form::kAttrReorderable, true);
			servers.AddString(form::kAttrUpdateMsg, kRowCommand);
			servers.AddInt32(form::kAttrOrder, order++);
			schema->AddMessage(kServers, &servers);
			int32 columnOrder = 0;
			struct Column {
				const char* name;
				const char* label;
				bool readOnly;
				bool hidden;
			};
			const Column columns[] = {
				{"enabled", "Use", false, false}, {"name", "Server", true, false}, {"info", "Description", true, false}, {"status", "Status", true, false},
				{"id", "", true, true}, {"status_state", "", true, true}, {"status_detail", "", true, true}, {"testing", "", true, true},
			};
			for (const Column& column : columns) {
				BMessage attributes;
				if (column.label[0] != '\0')
					attributes.AddString(form::kAttrLabel, column.label);
				if (column.readOnly)
					attributes.AddBool(form::kAttrReadOnly, true);
				if (column.hidden)
					attributes.AddBool(form::kAttrHidden, true);
				if (std::string(column.name) == "status") {
					// (the icon of the state before the text, the longer text as the tool tip, a spinner while the server is tested)
					attributes.AddString(form::kAttrStateField, "status_state");
					attributes.AddString(form::kAttrDetailField, "status_detail");
					attributes.AddString(form::kAttrBusyField, "testing");
				}
				attributes.AddInt32(form::kAttrOrder, columnOrder++);
				schema->AddMessage((std::string(kServers) + "." + column.name).c_str(), &attributes);
			}
			BMessage keyField;
			keyField.AddString(form::kAttrLabel, "API key");
			keyField.AddString(form::kAttrHelp, "The key of the server that is selected in the list. Only for a server that needs one (NextGIS, Overpass.ch, a custom mirror that asks for one).");
			keyField.AddString(form::kAttrKind, form::kKindSecret);
			keyField.AddString(form::kAttrBindToUpdateMsg, kRowCommand);
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
}


status_t
SettingsProvider::Apply(const BMessage& changes, std::vector<std::string>* refused)
{
	std::vector<std::pair<std::string, std::string>> valid;
	status_t first = B_OK;
	auto refuse = [&](const std::string& what, status_t why) {
		refused->push_back(what);
		if (first == B_OK)
			first = why;
	};

	// the list of the servers as it is now; changed by a cell or by the whole table, and written once (the order, and the ones that are on)
	Settings current;
	LoadSettings(&current);
	const std::vector<std::string> before = ServerOrder(current);
	std::vector<std::string> order = before;
	std::vector<std::string> on;
	for (const std::string& id : order) {
		if (ServerIsOn(current, id))
			on.push_back(id);
	}
	const std::vector<std::string> wasOn = on;
	std::vector<std::pair<std::string, std::string>> keys;
	std::vector<std::string> toTest;
	bool listChanged = false;

	char* name = nullptr;
	for (int32 i = 0; changes.GetInfo(B_ANY_TYPE, i, &name, nullptr, nullptr) == B_OK; i++) {
		const std::string key = name;
		if (key == kKeyField)
			continue;		// (the field of the key keeps nothing: the key is a cell of its row)
		if (key.compare(0, strlen(kServers), kServers) == 0) {
			// the list of the servers: the whole table (Revert, Defaults, a row moved), or a cell of a row (`overpass_servers_02.enabled`, `.overpass_key.value`); the row is the place in the
			// list as it was described
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
						refuse(key, B_BAD_VALUE);
						continue;
					}
					order.push_back(id);
					if (row.GetBool("enabled", false))
						on.push_back(id);
					BMessage part;
					const char* newKey = NULL;
					if (row.FindMessage(kKeyField, &part) == B_OK && part.FindString(form::kUpdateValue, &newKey) == B_OK && newKey != NULL
							&& st::Trim(newKey) != KeyOf(current, id)) {
						keys.push_back({KeySettingName(id), st::Trim(newKey)});
						toTest.push_back(id);
					}
				}
				// (what the table did not name keeps its place after the others)
				for (const std::string& id : before) {
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
			if (!ParseCellKey(key, kServers, &row, &column) || row < 0 || row >= (int32)before.size()) {
				refuse(key, B_NAME_NOT_FOUND);
				continue;
			}
			const std::string id = before[row];
			if (column == "enabled") {
				bool want = false;
				if (changes.FindBool(name, &want) != B_OK) {
					refuse(key, B_BAD_VALUE);
					continue;
				}
				on.erase(std::remove(on.begin(), on.end(), id), on.end());
				if (want)
					on.push_back(id);
				listChanged = true;
			} else if (column == std::string(kKeyField) + "." + form::kUpdateValue) {
				// (a cell is a field of the message, named by its path: the text is the string itself)
				std::string newKey, why;
				const char* typed = NULL;
				if (changes.FindString(name, &typed) == B_OK && typed != NULL)
					newKey = typed;
				if (typed == NULL || !ValidateSetting(KeySettingName(id), newKey, &why)) {
					refuse(why.empty() ? key : key + " (" + why + ")", B_BAD_VALUE);
					continue;
				}
				keys.push_back({KeySettingName(id), st::Trim(newKey)});
				toTest.push_back(id);
			} else {
				refuse(key, B_NAME_NOT_FOUND);
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
			refuse(why.empty() ? key : key + " (" + why + ")", field == nullptr ? B_NAME_NOT_FOUND : B_BAD_VALUE);
			continue;
		}
		if (key == "overpass_custom_key")
			text = st::ParseBool(text, false) ? "yes" : "no";
		valid.push_back({key, st::Trim(text)});
		// (the address and the flag of the custom mirror change what it is asked)
		if (key == "overpass_url" || key == "overpass_custom_key")
			toTest.push_back("custom");
	}
	if (listChanged) {
		valid.push_back({"overpass_servers", ServersText(order, on)});
		// a server that is switched on is tested
		for (const std::string& id : on) {
			if (std::find(wasOn.begin(), wasOn.end(), id) == wasOn.end())
				toTest.push_back(id);
		}
	}
	valid.insert(valid.end(), keys.begin(), keys.end());
	if (valid.empty())
		return first;

	const status_t result = WriteKeys(valid);
	if (result == B_OK) {
		// the servers whose switch, key or address changed are tested when they are on (a server that is off is not asked, and a key that is only changed is not a reason to ask it)
		Settings now;
		LoadSettings(&now);
		std::vector<std::string> done;
		for (const std::string& id : toTest) {
			if (std::find(done.begin(), done.end(), id) != done.end())
				continue;
			done.push_back(id);
			if (ServerIsOn(now, id))
				StartTest(id);
		}
	}
	return result != B_OK ? result : first;
}

}	// namespace spatial
