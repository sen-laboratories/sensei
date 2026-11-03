/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "BertSettingsForm.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <sen/SenMessagePath.h>
#include <sen/SenSettingsForm.h>

#include "../../common/SettingsText.h"

namespace form = sen::settings::form;
namespace st = sensei::settingstext;

namespace bert {

namespace {

const char* const kSettingsFile = "sen/bert.settings";

/** One setting: the key of the file (the name of the field), whether it is a yes/no, and what the form shows. */
struct Field {
	const char*	key;
	bool		isBool;
	const char*	label;
	const char*	help;
};

const Field kFields[] = {
	{"choose", true, "Ask which book if several are found", "OpenLibrary may find several books for a title. Off: the best one is taken."},
	{"authors", true, "Make people of the authors", "Each author becomes a file next to the book (with biography and photo), related to it by Authorship."},
	{"max_authors", false, "Most authors to make", "For books with a long list of authors."},
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
	message->AddBool("choose", s.choose);
	message->AddBool("authors", s.authors);
	message->AddInt32("max_authors", s.maxAuthors);
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
		if (!field.isBool) {
			attributes.AddDouble(form::kAttrMin, 1);
			attributes.AddDouble(form::kAttrMax, 100);
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
		if (field->isBool)
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

}	// namespace bert
