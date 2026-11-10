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
	{"email_services", kBool, "Photo and profile by e-mail address (Libravatar, Gravatar)",
		"Ask Libravatar and Gravatar for a photo and a profile of a person that has an e-mail address. They are given the SHA-256 hash of the address, never the address: but whoever knows an address can work out its hash, so a service learns that this address is looked up."},
	{"github", kBool, "GitHub", "Ask GitHub about a person whose links (schema:sameAs) name a GitHub account: its profile and its photo. A nickname alone is never taken for an account."},
	{"fediverse", kBool, "Fediverse (Mastodon and others)", "Ask the server of a person's Fediverse account (a link like https://host/@name, or @name@host as the nickname) for its photo, its text and its posts."},
	{"bluesky", kBool, "Bluesky", "Ask Bluesky about a person whose links name a Bluesky profile: its photo, its text and its posts."},
	{"feeds", kBool, "Feeds of homepages and accounts", "Look at the homepage of a person for the address of its feed (an RSS or Atom feed), and read the feeds of a person: the posts that the relation Status shows."},
	{"photo", kBool, "Photo as the thumbnail", "Write the photo that was found into the file as its thumbnail, with where it is from and its license."},
	{"be_people", kChoice, "Fill the attributes of People", "Put the name and the nickname into the attributes of the People application of Haiku (Contact name and Nickname: META:name, META:nickname), where they are empty (what is there is never replaced), so that the columns of the People window show them. By default only the contact files of People (application/x-person) get them, not the Person of SEN."},
	{"mastodon_servers", kText, "Mastodon servers to search", "The servers of the Fediverse whose search for accounts is asked when a person is looked for by its name (a server knows the accounts that it has seen), separated by blanks. A handle of the form user@server is asked at its own server."},
	{"photo_size", kNumber, "Size of the photo (pixels)", "The longer side of the photo: a larger photo is made smaller before it is written to the file (a thumbnail is a few hundred pixels, not a megabyte)."},
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
	message->AddBool("email_services", s.emailServices);
	message->AddBool("github", s.github);
	message->AddBool("fediverse", s.fediverse);
	message->AddBool("bluesky", s.bluesky);
	message->AddBool("feeds", s.feeds);
	message->AddBool("photo", s.photo);
	message->AddString("be_people", s.bePeople.c_str());
	message->AddInt32("photo_size", s.photoSize);
	std::string servers;
	for (const std::string& server : s.mastodonServers)
		servers += (servers.empty() ? "" : " ") + server;
	message->AddString("mastodon_servers", servers.c_str());

	// the services that are searched for a person by name or handle: a row for each, in the order of the list
	const std::vector<std::string> on = s.HuntServices();
	for (const std::string& id : s.HuntOrder()) {
		BMessage row;
		row.AddString("name", HuntServiceName(id));
		row.AddString("id", id.c_str());
		row.AddString("info", HuntServiceInfo(id));
		row.AddBool("enabled", std::find(on.begin(), on.end(), id) != on.end());
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
	services.AddString(form::kAttrLabel, "Search for people on");
	services.AddString(form::kAttrHelp, "The services that are asked, in this order, when a person has a name or a handle but nothing that says which account is its own. Drag a row to change the order. A name is sent to the services that are on; they are all public places where people have put themselves.");
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
			const char* const values[] = { "off", "legacy", "all" };
			const char* const labels[] = { "Off", "Only the contacts of People (application/x-person)", "All people files (also the Person of SEN)" };
			for (int i = 0; i < 3; i++) {
				attributes.AddString(form::kAttrChoices, values[i]);
				attributes.AddString(form::kAttrChoiceLabels, labels[i]);
			}
		}
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
	const std::vector<std::string> orderBefore = current.HuntOrder();
	std::vector<std::string> order = orderBefore;
	std::vector<std::string> on = current.HuntServices();
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
				if (*HuntServiceName(id) == '\0') {
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
			if (dot == std::string::npos || key.substr(dot + 1) != "enabled" || row < 0 || row >= (int)orderBefore.size() || changes.FindBool(name, &enabled) != B_OK) {
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
		valid.push_back({"hunt", line});
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
