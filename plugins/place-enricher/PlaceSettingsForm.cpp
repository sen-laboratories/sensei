/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PlaceSettingsForm.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <sen/SenMessagePath.h>
#include <sen/SenSettingsForm.h>

#include "../../common/SettingsText.h"

namespace form = sen::settings::form;
namespace st = sensei::settingstext;

namespace place {

namespace {

const char* const kSettingsFile = "sen/suess.settings";

enum FieldType {
	kBool,
	kNumber,
	kText
};

/** One setting: the key of the file (the name of the field), its type, and what the form shows. */
struct Field {
	const char*	key;
	FieldType	type;
	const char*	label;
	const char*	help;
};

const Field kFields[] = {
	{"language", kText, "Language", "The language of the names and descriptions of places. The first entry is the language of the system."},
	{"lookup_by_position", kBool, "Use GPS location for lookup", "Look up a photo or another file by its GPS location (the attributes schema:latitude and schema:longitude) when it has no title or name of a place to search for. OFF by default: the location of your file is then sent to the Nominatim service of OpenStreetMap. A Place or Location file with a location and no name is always looked up by it, and a location that only ranks the answers of a search by name is never sent."},
	{"picture", kBool, "Picture of the place", "Use a picture that Wikidata names (a photo, the coat of arms, the flag, a map) as the thumbnail, with its license and author. Two more requests, to Wikidata and Wikimedia Commons."},
	{"boundary", kBool, "Keep the outline of the place", "The simplified outline is what tells where places are in relation to each other (within, touches, overlaps). A large outline takes room in the file."},
};

const struct {
	const char* code;
	const char* label;
} kLanguages[] = {
	{"", "System language"}, {"en", "English"}, {"de", "Deutsch"}, {"fr", "Français"}, {"es", "Español"}, {"it", "Italiano"}, {"pt", "Português"},
	{"nl", "Nederlands"}, {"sv", "Svenska"}, {"pl", "Polski"}, {"ru", "Русский"}, {"ja", "日本語"},
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
	message->AddString("language", s.language.c_str());
	message->AddBool("lookup_by_position", s.lookupByPosition);
	message->AddBool("picture", s.picture);
	message->AddBool("boundary", s.boundary);
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
		if (key == "language") {
			attributes.AddString(form::kAttrKind, form::kKindChoice);
			for (const auto& language : kLanguages) {
				attributes.AddString(form::kAttrChoices, language.code);
				attributes.AddString(form::kAttrChoiceLabels, language.label);
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
		if (field->type == kBool)
			text = st::ParseBool(text, false) ? "yes" : "no";
		valid.push_back({key, text});
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

}	// namespace place
