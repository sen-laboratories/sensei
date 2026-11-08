/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialSettingsForm.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <sen/SenMessagePath.h>
#include <sen/SenSettingsForm.h>

#include "../../common/SettingsText.h"

namespace form = sen::settings::form;
namespace st = sensei::settingstext;

namespace spatial {

namespace {

const char* const kSettingsFile = "sen/spatial-navigator.settings";

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
}


status_t
SettingsProvider::Apply(const BMessage& changes, std::vector<std::string>* refused)
{
	std::vector<std::pair<std::string, std::string>> valid;
	status_t first = B_OK;

	char* name = nullptr;
	for (int32 i = 0; changes.GetInfo(B_ANY_TYPE, i, &name, nullptr, nullptr) == B_OK; i++) {
		const std::string key = name;
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
