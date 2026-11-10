/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PersonSettingsForm.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include <algorithm>
#include <cstdlib>

#include <sen/SenMessagePath.h>
#include <sen/SenSettingsForm.h>

#include "../../common/SettingsText.h"

namespace form = sen::settings::form;
namespace st = sensei::settingstext;

namespace robin {

namespace {

const char* const kSettingsFile = "sen/robin.settings";

enum FieldType {
	kBool,
	kNumber,
	kText,
	kList,		///< several texts, in a list
	kChoice		///< one of a few values, as radio buttons
};

/** One setting: the key of the file (the name of the field), its type, and what the form shows. */
struct Field {
	const char*	key;
	FieldType	type;
	const char*	label;
	const char*	help;
};

const Field kFields[] = {
	{"photo", kBool, "Photo as the thumbnail", "Writes the photo into the file, with its source and license."},
	{"be_people", kChoice, "Fill the attributes of People", "Contact name and Nickname (META:name, META:nickname), where they are empty."},
	{"name_correction", kChoice, "When an account spells a name differently", "Ask: Enrich (Overwrite) corrects the name and renames the file."},
	{"mastodon_servers", kList, "Mastodon servers to search", "Asked when a person is searched by name."},
	{"photo_size", kNumber, "Size of the photo (pixels)", "The longer side; a larger photo is made smaller."},
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
	message->AddBool("photo", s.photo);
	message->AddString("be_people", s.bePeople.c_str());
	message->AddString("name_correction", s.nameCorrection.c_str());
	message->AddInt32("photo_size", s.photoSize);
	for (const std::string& server : s.mastodonServers)
		message->AddString("mastodon_servers", server.c_str());

	// the services that are searched for a person by name or handle: a row for each, in the order of the list
	for (const std::string& id : s.ServiceOrder()) {
		BMessage row;
		row.AddString("name", ServiceName(id));
		row.AddString("id", id.c_str());
		row.AddString("info", ServiceInfo(id));
		row.AddBool("enabled", s.Uses(id));
		message->AddMessage("services", &row);
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
	// the services of the search by name: a box for each, in the order that they are asked (drag a row to change it)
	BMessage services;
	services.AddString(form::kAttrLabel, "Services");
	services.AddString(form::kAttrHelp, "Asked in this order: what an earlier service gave is kept. Drag a row to move it.");
	services.AddString(form::kAttrKind, form::kKindTable);
	services.AddBool(form::kAttrReorderable, true);
	services.AddInt32(form::kAttrOrder, 0);
	schema->AddMessage("services", &services);
	BMessage use;
	use.AddString(form::kAttrLabel, "Use");
	use.AddInt32(form::kAttrOrder, 0);
	schema->AddMessage("services.enabled", &use);
	BMessage serviceName;
	serviceName.AddString(form::kAttrLabel, "Service");
	serviceName.AddBool(form::kAttrReadOnly, true);
	serviceName.AddInt32(form::kAttrOrder, 1);
	schema->AddMessage("services.name", &serviceName);
	BMessage serviceInfo;
	serviceInfo.AddString(form::kAttrLabel, "Info");
	serviceInfo.AddBool(form::kAttrReadOnly, true);
	serviceInfo.AddInt32(form::kAttrOrder, 2);
	schema->AddMessage("services.info", &serviceInfo);
	BMessage serviceId;
	serviceId.AddBool(form::kAttrHidden, true);
	schema->AddMessage("services.id", &serviceId);

	int32 order = 1;
	for (const Field& field : kFields) {
		BMessage attributes;
		attributes.AddString(form::kAttrLabel, field.label);
		attributes.AddString(form::kAttrHelp, field.help);
		attributes.AddInt32(form::kAttrOrder, order++);
		if (field.type == kNumber) {
			attributes.AddString(form::kAttrKind, form::kKindSpinner);
			attributes.AddDouble(form::kAttrMin, kMinPhotoSize);
			attributes.AddDouble(form::kAttrMax, kMaxPhotoSize);
			attributes.AddDouble(form::kAttrStep, 32);
		}
		if (field.type == kChoice) {
			attributes.AddString(form::kAttrKind, form::kKindRadio);
			std::vector<std::pair<const char*, const char*>> choices;
			if (strcmp(field.key, "be_people") == 0)
				choices = { {"off", "Off"}, {"legacy", "Only legacy Person files (x-person)"}, {"all", "All Person files"} };
			else
				choices = { {"ask", "Ask me"}, {"always", "Always correct the name and rename the file"}, {"never", "Never"} };
			for (const auto& choice : choices) {
				attributes.AddString(form::kAttrChoices, choice.first);
				attributes.AddString(form::kAttrChoiceLabels, choice.second);
			}
		}
		if (field.type == kList)
			attributes.AddBool(form::kAttrList, true);
		// (the size of the photo means something only while the photo is written)
		if (strcmp(field.key, "photo_size") == 0)
			attributes.AddString(form::kAttrEnabledWhen, "photo=true");
		schema->AddMessage(field.key, &attributes);
	}
}


status_t
SettingsProvider::Apply(const BMessage& changes, std::vector<std::string>* refused)
{
	std::vector<std::pair<std::string, std::string>> valid;
	status_t first = B_OK;

	// the services of the search: a cell of the table by its row (`services_01.enabled`), or the whole table (a move of a row, Revert, Defaults)
	Settings current;
	LoadSettings(&current);
	const std::vector<std::string> orderBefore = current.ServiceOrder();
	std::vector<std::string> order = orderBefore;
	std::vector<std::string> on;
	for (const std::string& id : orderBefore) {
		if (current.Uses(id))
			on.push_back(id);
	}
	bool servicesChanged = false;

	char* name = nullptr;
	for (int32 i = 0; changes.GetInfo(B_ANY_TYPE, i, &name, nullptr, nullptr) == B_OK; i++) {
		const std::string key = name;
		if (key == "services") {
			type_code type = 0;
			int32 count = 0;
			changes.GetInfo(name, &type, &count);
			order.clear();
			on.clear();
			for (int32 row = 0; row < count; row++) {
				BMessage each;
				if (changes.FindMessage(name, row, &each) != B_OK)
					continue;
				const std::string id = each.GetString("id", "");
				if (*ServiceName(id) == '\0') {
					refused->push_back("services");
					continue;
				}
				if (std::find(order.begin(), order.end(), id) == order.end())
					order.push_back(id);
				if (each.GetBool("enabled", false))
					on.push_back(id);
			}
			for (const std::string& id : orderBefore) {
				if (std::find(order.begin(), order.end(), id) == order.end())
					order.push_back(id);
			}
			servicesChanged = true;
			continue;
		}
		if (key.compare(0, 9, "services_") == 0) {
			const size_t dot = key.find('.');
			const int row = dot == std::string::npos ? -1 : atoi(key.substr(9, dot - 9).c_str());
			bool enabled = false;
			const std::string column = dot == std::string::npos ? std::string() : key.substr(dot + 1);
			if (column != "enabled" || row < 0 || row >= (int)orderBefore.size() || changes.FindBool(name, &enabled) != B_OK) {
				refused->push_back(key);
				if (first == B_OK)
					first = B_BAD_VALUE;
				continue;
			}
			const std::string& id = orderBefore[row];
			on.erase(std::remove(on.begin(), on.end(), id), on.end());
			if (enabled)
				on.push_back(id);
			servicesChanged = true;
			continue;
		}
		const Field* field = nullptr;
		for (const Field& each : kFields) {
			if (key == each.key)
				field = &each;
		}
		std::string text, why;
		if (field != nullptr && field->type == kList) {
			// (a list: all its values, written in one line separated by blanks)
			type_code listType = 0;
			int32 count = 0;
			changes.GetInfo(name, &listType, &count);
			for (int32 value = 0; value < count; value++) {
				const std::string one = changes.GetString(name, value, "");
				if (!one.empty())
					text += (text.empty() ? "" : " ") + one;
			}
			valid.push_back({key, text});
			continue;
		}
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
	if (servicesChanged) {
		std::string line;
		for (const std::string& id : order)
			line += (line.empty() ? "" : " ") + std::string(std::find(on.begin(), on.end(), id) != on.end() ? "" : "-") + id;
		valid.push_back({"services", line});
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

}	// namespace robin
