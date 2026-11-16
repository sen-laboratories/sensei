/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "CalendarSettingsForm.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Query.h>
#include <String.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include <sen/SenMessagePath.h>
#include <sen/SenOntoPeople.h>

#include "../../common/SettingsText.h"
#include "core/CalendarFacts.h"

namespace form = sen::settings::form;
namespace st = sensei::settingstext;

namespace calendar {

namespace {

const char* const kSettingsFile = "sen/calendar-extractor.settings";
std::mutex sFileLock;

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

/** the people that SEN knows, as `Name <address>` (or the name): the values of the list that follows the field "Myself" */
std::vector<std::string>
PeopleValues()
{
	std::vector<std::string> values;
	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK && values.size() < 2000) {
		if (!volume.KnowsQuery())
			continue;
		BQuery query;
		query.SetVolume(&volume);
		query.PushAttr("BEOS:TYPE");
		query.PushString(sen::onto::people::mime::kPerson);
		query.PushOp(B_EQ);
		if (query.Fetch() != B_OK)
			continue;
		BEntry entry;
		while (query.GetNextEntry(&entry) == B_OK && values.size() < 2000) {
			char name[B_FILE_NAME_LENGTH];
			if (entry.GetName(name) != B_OK)
				continue;
			BNode node(&entry);
			BString mbox;
			std::string text = name;
			if (node.ReadAttrString(sen::onto::people::attr::kMbox, &mbox) == B_OK && !mbox.IsEmpty())
				text += std::string(" <") + mbox.String() + ">";
			values.push_back(text);
		}
	}
	std::sort(values.begin(), values.end());
	values.erase(std::unique(values.begin(), values.end()), values.end());
	return values;
}

void
ToMessage(const Settings& s, BMessage* message)
{
	message->MakeEmpty();
	message->AddBool("rename", s.rename);
	message->AddBool("description", s.description);
	message->AddString("myself", s.myself.c_str());
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

	BMessage rename;
	rename.AddString(form::kAttrLabel, "Name the file after the event");
	rename.AddString(form::kAttrHelp, "A calendar file with one event is renamed to its date, time and title: 2026-11-12_0930_Planning.ics");
	rename.AddInt32(form::kAttrOrder, order++);
	schema->AddMessage("rename", &rename);

	BMessage description;
	description.AddString(form::kAttrLabel, "Keep the description in a Markdown file");
	description.AddString(form::kAttrHelp, "The description of the event is written as Markdown, with its links and formatting, to a file with the name of the event beside it.");
	description.AddInt32(form::kAttrOrder, order++);
	schema->AddMessage("description", &description);

	// who the user is: the people that SEN knows are listed, and what is typed filters the list; a name and an address that is none of them is fine (Name <address>)
	BMessage myself;
	myself.AddString(form::kAttrLabel, "Myself");
	myself.AddString(form::kAttrHelp, "Who you are, as Name <address>: choose a person or type it. The attendee that is you is told in the relation Attendee.");
	myself.AddString(form::kAttrKind, form::kKindPopup);
	for (const std::string& person : PeopleValues())
		myself.AddString(form::kAttrChoices, person.c_str());
	myself.AddBool(form::kAttrAllowCustom, true);
	myself.AddBool(form::kAttrAllowMultiple, false);
	myself.AddInt32(form::kAttrOrder, order++);
	schema->AddMessage("myself", &myself);
}


status_t
SettingsProvider::Apply(const BMessage& changes, std::vector<std::string>* refused)
{
	std::vector<std::pair<std::string, std::string>> valid;
	status_t first = B_OK;
	char* name = nullptr;
	for (int32 i = 0; changes.GetInfo(B_ANY_TYPE, i, &name, nullptr, nullptr) == B_OK; i++) {
		const std::string key = name;
		std::string text, why;
		if (sen::settings::GetText(changes, key, &text) != B_OK || !ValidateSetting(key, text, &why)) {
			refused->push_back(why.empty() ? key : key + " (" + why + ")");
			if (first == B_OK)
				first = why.empty() ? B_NAME_NOT_FOUND : B_BAD_VALUE;
			continue;
		}
		if (key == "rename" || key == "description")
			text = st::ParseBool(text, false) ? "yes" : "no";
		valid.push_back({key, st::Trim(text)});
	}
	if (valid.empty())
		return first;
	const status_t result = WriteKeys(valid);
	return result != B_OK ? result : first;
}

}	// namespace calendar
