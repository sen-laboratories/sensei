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
		"How far around a place or location what is nearby is looked for, in metres. The area is at least twice this across; a place that is larger is searched in its own box (up to 3 km across)."},
	{"poi_limit", "Most things nearby",
		"How many things nearby at most. The notable ones (sights with a Wikidata item or a Wikipedia article) come before shops of the same distance, then the nearest."},
	{"poi_cache_hours", "Keep answers for (hours)",
		"How long what OpenStreetMap said about the surroundings of a place is kept. A place is not remapped that often, so a day is enough: a folder of what is nearby that is opened later asks again. 1 to 168 hours."},
	{"map_style", "Map style",
		"The default style of the map of a place (Shoji): the tiles of OpenStreetMap data from public servers, without a key. The map says whose it is. A view of the map can choose another style "
		"with the secondary mouse button, and go back to this one."},
	{"overpass_mirror", "Overpass server",
		"The server that is asked for what is nearby a place (OpenStreetMap, Overpass API). The public server is often busy; the mirrors give the same data and have other limits. "
		"The question and the box of the place go to the server that is chosen."},
	{"overpass_url", "Custom mirror URL",
		"The address of the interpreter of the mirror, like https://overpass.example.org/api/interpreter. For a server that takes the key in the path, put {key} where it goes. Enabled for the custom mirror."},
	{"overpass_custom_key", "The custom mirror requires an API key",
		"Tick this if the custom mirror wants a key. The field of the key is enabled only for a server that requires one. Enabled for the custom mirror."},
	{"overpass_key", "API key",
		"The key of a server that requires one (NextGIS, Overpass.ch: sign up there for it; or a custom mirror that you ticked). It is put in the address of the server in place of {key}, else sent as Authorization: Bearer <key> over https. It is kept in the settings file as it is typed."},
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
	message->AddString("overpass_mirror", s.overpassMirror.c_str());
	message->AddString("overpass_url", s.overpassUrl.c_str());
	message->AddBool("overpass_custom_key", s.customRequiresKey);
	message->AddString("overpass_key", s.overpassKey.c_str());
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
	const std::string chosen = OverpassEndpoint(settings);
	const std::string host = HostOf(chosen);
	const std::string key = OverpassKey(settings);
	const std::string endpoint = WithKey(chosen, key);
	std::string authorization;
	std::string extra;
	if (!key.empty() && chosen.find("{key}") == std::string::npos) {
		if (endpoint.compare(0, 8, "https://") == 0)
			authorization = "Bearer " + key;
		else
			extra = "The key is not sent: the address is not https.";
	}
	// (a custom mirror that is not complete, or a server that wants a key and has none: the navigator asks the public server then, and the test says so)
	if (settings.overpassMirror != "default" && chosen == kDefaultOverpassUrl)
		extra += std::string(extra.empty() ? "" : "\n") + "The chosen mirror is not complete: the public server is used.";
	// an address that is only a host: the interpreter of an Overpass server is at a path of its own, mostly /api/interpreter
	{
		const size_t scheme = chosen.find("://");
		const size_t slash = chosen.find('/', scheme == std::string::npos ? 0 : scheme + 3);
		if (settings.overpassMirror == "custom" && (slash == std::string::npos || slash + 1 >= chosen.size()))
			extra += std::string(extra.empty() ? "" : "\n") + "The address has no path: the interpreter is usually at .../api/interpreter.";
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
		} else if (key == "overpass_mirror") {
			attributes.AddString(form::kAttrKind, form::kKindChoice);
			for (const Mirror& mirror : Mirrors()) {
				// (the servers of the list, then a line, then the choice of one's own)
				if (std::string(mirror.id) == "custom") {
					attributes.AddString(form::kAttrChoices, form::kChoiceSeparator);
					attributes.AddString(form::kAttrChoiceLabels, "");
				}
				attributes.AddString(form::kAttrChoices, mirror.id);
				attributes.AddString(form::kAttrChoiceLabels, mirror.label);
			}
		} else {
			// (the address and the key belong to the custom mirror: the fields can be edited while it is chosen)
			// the address belongs to the custom mirror alone; the key to it and to the servers that want one
			// the address and the flag belong to the custom mirror alone; the key is for a server that requires one
			attributes.AddString(form::kAttrEnabledWhen, key == "overpass_key" ? KeyEnabledWhen().c_str() : "overpass_mirror=custom");
			if (key == "overpass_key")
				attributes.AddString(form::kAttrKind, form::kKindSecret);
		}
		schema->AddMessage(field.key, &attributes);
	}

	// a button under the settings of the server: does it work? (the answer is shown next to it)
	BMessage test;
	test.AddString(form::kAttrLabel, "Test the server");
	test.AddString(form::kAttrKind, form::kKindButton);
	test.AddString(form::kAttrShows, kTestResult);
	test.AddString(form::kAttrHelp, "Asks the server that is chosen above (with its key) a small question about the centre of Wien, and tells whether it answers and how fast. "
		"Change the settings first: they are used as they are shown.");
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

	char* name = nullptr;
	for (int32 i = 0; changes.GetInfo(B_ANY_TYPE, i, &name, nullptr, nullptr) == B_OK; i++) {
		const std::string key = name;
		if (key == kTestButton) {
			// (the button: the server of the settings as they are in the file now; the answer is shown next to it)
			Settings current;
			LoadSettings(&current);
			fTest = TestOverpassServer(current);
			continue;
		}
		if (key == kTestResult || key == kTestState || key == kTestDetail)
			continue;
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
	if (valid.empty())
		return first;

	BPath path;
	if (!SettingsPath(&path))
		return B_ERROR;
	BPath folder;
	if (path.GetParent(&folder) == B_OK)
		create_directory(folder.Path(), 0755);

	const std::string text = st::Update(ReadFileText(), valid);
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	status_t result = file.InitCheck();
	if (result == B_OK && file.Write(text.data(), text.size()) != (ssize_t)text.size())
		result = B_IO_ERROR;
	return result != B_OK ? result : first;
}

}	// namespace spatial
