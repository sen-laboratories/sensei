/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "MovieSettingsForm.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <sen/SenMessagePath.h>
#include <sen/SenSettingsForm.h>

#include "../../common/SettingsText.h"

namespace form = sen::settings::form;
namespace st = sensei::settingstext;

namespace movie {

namespace {

const char* const kSettingsFile = "sen/movies.settings";

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
	{"api_key", kText, "TMDB API key", "The key (v3) of The Movie Database: free, at themoviedb.org/settings/api (Settings, API). Without one nothing can be looked up. It is kept in the settings file as it is."},
	{"language", kText, "Language", "The language of titles, descriptions and biographies that TMDB gives."},
	{"choose", kBool, "Ask which movie if several are found", "Off: the best one is taken."},
	{"crew", kBool, "Make people of the director and the crew", "Writers, producers, composer, cinematography and editing: each is a person with a Contribution to the movie."},
	{"cast", kBool, "Make people of the cast", "Each actor is a person with a Contribution (the role Actor, the character, the place in the credits)."},
	{"max_cast", kNumber, "Most actors to make", "The first actors of the credits."},
};

const struct {
	const char* code;
	const char* label;
} kLanguages[] = {
	{"en-US", "English"}, {"de-DE", "Deutsch"}, {"fr-FR", "Français"}, {"es-ES", "Español"}, {"it-IT", "Italiano"}, {"pt-BR", "Português (Brasil)"},
	{"nl-NL", "Nederlands"}, {"sv-SE", "Svenska"}, {"pl-PL", "Polski"}, {"ru-RU", "Русский"}, {"ja-JP", "日本語"},
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
	message->AddString("api_key", s.apiKey.c_str());
	message->AddString("language", s.language.c_str());
	message->AddBool("choose", s.choose);
	message->AddBool("crew", s.crew);
	message->AddBool("cast", s.cast);
	message->AddInt32("max_cast", s.maxCast);
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
		if (key == "api_key") {
			attributes.AddString(form::kAttrKind, form::kKindSecret);
		} else if (key == "language") {
			attributes.AddString(form::kAttrKind, form::kKindChoice);
			for (const auto& language : kLanguages) {
				attributes.AddString(form::kAttrChoices, language.code);
				attributes.AddString(form::kAttrChoiceLabels, language.label);
			}
		} else if (key == "max_cast") {
			attributes.AddDouble(form::kAttrMin, 0);
			attributes.AddDouble(form::kAttrMax, 50);
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
		std::string text;
		if (field == nullptr || sen::settings::GetText(changes, key, &text) != B_OK || !ValidateSetting(key, text)) {
			refused->push_back(key);
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

}	// namespace movie
