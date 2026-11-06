/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperSettingsForm.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <sen/SenMessagePath.h>
#include <sen/SenSettingsForm.h>

#include "PaperSettings.h"

namespace form = sen::settings::form;

namespace paperboy {

namespace {

/** the file with the settings, next to those of SEN */
const char* const kSettingsFile = "sen/paperboy.settings";

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
	{"email", kText, "E-mail address", "Unpaywall and OpenAlex ask who you are (their polite pool); Unpaywall refuses requests without an address."},
	{"api_key", kText, "OpenAlex key", "Free, from openalex.org/settings/api. Without one all requests of an address share a small daily budget. The key is kept in the settings file as it is."},
	{"unpaywall", kBool, "Ask Unpaywall for the open access status", "Needs an address; else the data of OpenAlex is used."},
	{"title_search", kBool, "Look a paper up by its title if there is no DOI", ""},
	{"semantic_scholar", kBool, "Ask Semantic Scholar if OpenAlex does not know the paper", ""},
	{"dblp", kBool, "Ask DBLP if OpenAlex does not know the paper", ""},
	{"arxiv", kBool, "Ask arXiv if OpenAlex does not know the paper", "A paper whose document names an arXiv identifier is looked up there first."},
	{"overwrite", kBool, "Replace what the file already has", "Off: only the missing attributes are filled."},
	{"rename", kBool, "Rename the file after the paper", "The names of papers say nothing (the numbers of a conference)."},
	{"rename_pattern", kText, "Name pattern", "Tokens: {year} {first_author} {title} {doi}. {title} gives Deep learning.pdf, {year}_{first_author}_{title} gives 2015_LeCun_Deep learning.pdf."},
	{"rename_underscores", kBool, "Underscores instead of blanks in the new name", ""},
	{"authors", kText, "Authors become people", "The authors of an enriched paper are made files next to it, with the Authorship relation."},
	{"max_pages", kNumber, "Pages to search for a DOI", "Of the beginning of the document."},
	{"local_only", kBool, "Extraction: only cited papers that are on this computer", ""},
};


const Field*
FindField(const std::string& key)
{
	for (const Field& field : kFields) {
		if (key == field.key)
			return &field;
	}
	return nullptr;
}


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


/** the settings as a message: the field of every setting, in its type */
void
ToMessage(const Settings& s, BMessage* message)
{
	message->MakeEmpty();
	message->AddString("email", s.email.c_str());
	message->AddString("api_key", s.apiKey.c_str());
	message->AddBool("unpaywall", s.unpaywall);
	message->AddBool("title_search", s.titleSearch);
	message->AddBool("semantic_scholar", s.semanticScholar);
	message->AddBool("dblp", s.dblp);
	message->AddBool("arxiv", s.arxiv);
	message->AddBool("overwrite", s.overwrite);
	message->AddBool("rename", s.rename);
	message->AddString("rename_pattern", s.renamePattern.c_str());
	message->AddBool("rename_underscores", s.renameUnderscores);
	message->AddString("authors", s.authors.c_str());
	message->AddInt32("max_pages", s.maxPages);
	message->AddBool("local_only", s.localOnly);
}

}	// namespace


void
SettingsProvider::Describe(BMessage* settings, BMessage* schema, BMessage* defaults)
{
	Settings current;
	ParseSettings(ReadFileText(), &current);
	ToMessage(current, settings);
	ToMessage(Settings(), defaults);

	schema->MakeEmpty();
	schema->AddInt32(form::kSchemaVersion, 1);
	int32 order = 0;
	for (const Field& field : kFields) {
		BMessage attributes;
		attributes.AddString(form::kAttrLabel, field.label);
		if (field.help[0] != '\0')
			attributes.AddString(form::kAttrHelp, field.help);
		attributes.AddInt32(form::kAttrOrder, order++);
		const std::string key = field.key;
		if (key == "api_key")
			attributes.AddString(form::kAttrKind, form::kKindSecret);
		else if (key == "authors") {
			attributes.AddString(form::kAttrKind, form::kKindChoice);
			attributes.AddString(form::kAttrChoices, "all");
			attributes.AddString(form::kAttrChoices, "orcid");
			attributes.AddString(form::kAttrChoices, "no");
			attributes.AddString(form::kAttrChoiceLabels, "All of the first authors");
			attributes.AddString(form::kAttrChoiceLabels, "Only those with an ORCID iD");
			attributes.AddString(form::kAttrChoiceLabels, "None");
		} else if (key == "max_pages") {
			attributes.AddDouble(form::kAttrMin, 1);
			attributes.AddDouble(form::kAttrMax, 20);
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
	type_code type = 0;
	for (int32 i = 0; changes.GetInfo(B_ANY_TYPE, i, &name, &type, nullptr) == B_OK; i++) {
		const std::string key = name;
		const Field* field = FindField(key);
		std::string text, why;
		// (the field is read by its name, which is a key of the file: no dots, no index)
		if (field == nullptr || sen::settings::GetText(changes, key, &text) != B_OK || !ValidateSetting(key, text, &why)) {
			refused->push_back(why.empty() ? key : key + " (" + why + ")");
			if (first == B_OK)
				first = field == nullptr ? B_NAME_NOT_FOUND : B_BAD_VALUE;
			continue;
		}
		if (field->type == kBool)
			text = ParseBool(text, false) ? "yes" : "no";
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

	const std::string text = UpdateSettingsText(ReadFileText(), valid);
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	status_t result = file.InitCheck();
	if (result == B_OK && file.Write(text.data(), text.size()) != (ssize_t)text.size())
		result = B_IO_ERROR;
	return result != B_OK ? result : first;
}

}	// namespace paperboy
