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
	kNumber
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
	message->AddInt32("photo_size", s.photoSize);
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
		if (field.type == kNumber) {
			attributes.AddString(form::kAttrKind, form::kKindSpinner);
			attributes.AddDouble(form::kAttrMin, kMinPhotoSize);
			attributes.AddDouble(form::kAttrMax, kMaxPhotoSize);
			attributes.AddDouble(form::kAttrStep, 32);
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

}	// namespace robin
