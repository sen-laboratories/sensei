/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "Authors.h"

#include <Directory.h>
#include <File.h>
#include <fs_attr.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <String.h>
#include <Url.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <sen/Sen.h>
#include <sen/SenMimeTypes.h>
#include <sen/SenOntoBooks.h>
#include <sen/SenOntoPeople.h>

#include "App.h"
#include "../../common/BaseEnricher.h"
#include "../../common/ThumbnailUtil.h"

static const char* kAuthorKeyAttr = "OPENLIB:author_key";

namespace {

/** the first number in a list from the JSON of OpenLibrary (keys "0", "1"... of a message) that is more than 0 (-1 is a photo that was taken down) */
int64
FirstPositive(const BMessage& list)
{
	char* name;
	type_code type;
	for (int32 index = 0; list.GetInfo(B_ANY_TYPE, index, &name, &type) == B_OK; index++) {
		int64 value = 0;
		switch (type) {
			case B_INT32_TYPE:	value = list.GetInt32(name, 0); break;
			case B_INT64_TYPE:	value = list.GetInt64(name, 0); break;
			case B_DOUBLE_TYPE:	value = (int64) list.GetDouble(name, 0); break;
			case B_FLOAT_TYPE:	value = (int64) list.GetFloat(name, 0); break;
			default:			continue;
		}
		if (value > 0)
			return value;
	}
	return 0;
}


/** the text of a field that is a string or, as the biography is, an object with the text as its value */
BString
TextOf(const BMessage& message, const char* field)
{
	BString text;
	if (message.FindString(field, &text) == B_OK)
		return text;
	BMessage object;
	if (message.FindMessage(field, &object) == B_OK && object.FindString("value", &text) == B_OK)
		return text;
	return BString();
}


/** the birthday as OpenLibrary has it ("1964-03-04", "4 March 1964", "1964"), or false */
bool
ParseDate(const BString& text, time_t* result)
{
	static const char* formats[] = {"%Y-%m-%d", "%d %B %Y", "%B %d, %Y", "%Y"};
	for (const char* format : formats) {
		struct tm parsed;
		memset(&parsed, 0, sizeof(parsed));
		const char* end = strptime(text.String(), format, &parsed);
		if (end != NULL && *end == '\0') {
			parsed.tm_isdst = -1;
			if (parsed.tm_mday == 0)
				parsed.tm_mday = 1;
			*result = mktime(&parsed);
			return *result != (time_t) -1;
		}
	}
	return false;
}


/** whether an attribute is there already */
bool
HasAttr(BNode& node, const char* name)
{
	attr_info info;
	return node.GetAttrInfo(name, &info) == B_OK;
}

}	// namespace


AuthorMaker::AuthorMaker(BaseEnricher* enricher, bool overwrite, bool debug)
	:
	fEnricher(enricher),
	fOverwrite(overwrite),
	fDebug(debug)
{
}


int32
AuthorMaker::MakeAuthors(const entry_ref& book, const BStringList& keys, const BStringList& names)
{
	int32 made = 0;
	for (int32 index = 0; index < keys.CountStrings(); index++) {
		const BString& key = keys.StringAt(index);
		if (key.IsEmpty())
			continue;

		BMessage author;
		status_t result = Fetch(key.String(), &author);
		if (result != B_OK) {
			printf("could not look up the author %s, skipping: %s\n", key.String(), strerror(result));
			continue;
		}

		BString name = TextOf(author, "name");
		if (name.IsEmpty() && index < names.CountStrings())
			name = names.StringAt(index);
		if (name.IsEmpty())
			name = key;

		entry_ref person;
		bool created = false;
		result = FindOrCreatePerson(book, name, key.String(), &person, &created);
		if (result != B_OK) {
			printf("could not make the author %s, skipping: %s\n", name.String(), strerror(result));
			continue;
		}
		printf("%s author '%s' (%s)...\n", created ? "making" : "updating", name.String(), key.String());

		WriteAttributes(person, author, name, key.String());

		// the photo, like the cover of the book
		int64 photoId = 0;
		BMessage photos;
		if (author.FindMessage("photos", &photos) == B_OK)
			photoId = FirstPositive(photos);
		if (photoId > 0) {
			std::string image;
			result = FetchPhoto(photoId, &image);
			if (result == B_OK && image.length() > 0) {
				result = WriteThumbnail(&person, image);
				if (result != B_OK)
					printf("error writing the photo of %s to the thumbnail: %s\n", name.String(), strerror(result));
			} else {
				printf("error fetching the photo of %s, skipping it.\n", name.String());
			}
		} else {
			printf("OpenLibrary has no photo of %s.\n", name.String());
		}

		result = Relate(person, book);
		if (result != B_OK) {
			printf("could not relate %s to the book: %s\n", name.String(), strerror(result));
			continue;
		}
		made++;
	}
	return made;
}


status_t
AuthorMaker::Fetch(const char* key, BMessage* author)
{
	BUrl url;
	BMessage params;
	params.AddString("id", key);
	status_t result = fEnricher->CreateHttpApiUrl(API_AUTHORS_URL, &params, &url);
	if (result != B_OK)
		return result;
	result = fEnricher->FetchRemoteJson(url, *author);
	if (result == B_OK && fDebug)
		author->PrintToStream();
	return result;
}


status_t
AuthorMaker::FetchPhoto(int64 photoId, std::string* image)
{
	BUrl url;
	BMessage params;
	BString id;
	id << photoId;
	params.AddString("photoId", id);
	params.AddString("size", "M");
	status_t result = fEnricher->CreateHttpApiUrl(API_AUTHOR_IMG_URL, &params, &url);
	if (result != B_OK)
		return result;
	return fEnricher->FetchRemoteContent(url, image);
}


/**
 * The person is a file in the folder of the book, named after the author. One that is there already is the same person if it has the key of the
 * author, or at least the type of a person (and no other key); a file of any other kind with that name is not touched: the person gets the key in its name.
 */
status_t
AuthorMaker::FindOrCreatePerson(const entry_ref& book, const BString& name, const char* key, entry_ref* person, bool* created)
{
	BEntry bookEntry(&book);
	BDirectory folder;
	status_t result = bookEntry.InitCheck();
	if (result == B_OK)
		result = bookEntry.GetParent(&folder);
	if (result != B_OK)
		return result;

	BString fileName(name);
	fileName.ReplaceAll('/', '-');	// (a file name cannot hold a path)
	fileName.Trim();
	if (fileName.IsEmpty())
		fileName = key;

	for (int attempt = 0; attempt < 2; attempt++) {
		BEntry entry(&folder, fileName.String());
		if (entry.Exists()) {
			BNode node(&entry);
			char type[B_MIME_TYPE_LENGTH];
			type[0] = '\0';
			BNodeInfo(&node).GetType(type);
			BString storedKey;
			node.ReadAttrString(kAuthorKeyAttr, &storedKey);
			bool samePerson = strcmp(type, sen::onto::people::mime::kPerson) == 0 && (storedKey.IsEmpty() || storedKey == key);
			if (!samePerson) {
				// somebody else, or another kind of file: the person is named with the key
				fileName << " (" << key << ")";
				continue;
			}
			*created = false;
			return entry.GetRef(person);
		}

		BFile file;
		result = folder.CreateFile(fileName.String(), &file, true);
		if (result != B_OK)
			return result;
		file.Sync();	// (the attributes are written to the file as it is now)
		BNodeInfo info(&file);
		result = info.SetType(sen::onto::people::mime::kPerson);
		if (result != B_OK)
			return result;
		*created = true;
		return BEntry(&folder, fileName.String()).GetRef(person);
	}
	return B_NAME_IN_USE;
}


void
AuthorMaker::WriteAttributes(const entry_ref& person, const BMessage& author, const BString& name, const char* key)
{
	BNode node(&person);
	if (node.InitCheck() != B_OK)
		return;

	auto writeString = [&](const char* attribute, const BString& value) {
		if (value.IsEmpty() || (!fOverwrite && HasAttr(node, attribute)))
			return;
		node.WriteAttr(attribute, B_STRING_TYPE, 0, value.String(), value.Length() + 1);
	};

	writeString(sen::onto::people::attr::kName, name);
	writeString(sen::onto::people::attr::kDescription, TextOf(author, "bio"));
	writeString(kAuthorKeyAttr, BString(key));

	time_t birthday;
	BString born = TextOf(author, "birth_date");
	if (!born.IsEmpty() && (fOverwrite || !HasAttr(node, sen::onto::people::attr::kBirthDate)) && ParseDate(born, &birthday))
		node.WriteAttr(sen::onto::people::attr::kBirthDate, B_TIME_TYPE, 0, &birthday, sizeof(birthday));
}


/** the person is the author of the book: the server writes the relation (and its opposite direction, "authored by") */
status_t
AuthorMaker::Relate(const entry_ref& person, const entry_ref& book)
{
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid())
		return B_NOT_INITIALIZED;	// the SEN server is not running: the person is there, the relation is made by the next enrichment

	BMessage add(sen::cmd::kRelationAdd), answer;
	add.AddString(sen::key::kRelationType, sen::onto::books::mime::kAuthorship);
	add.AddRef(sen::key::kSourceRef, &person);
	add.AddRef(sen::key::kTargetRef, &book);
	status_t result = server.SendMessage(&add, &answer, 10000000, 10000000);
	if (result != B_OK)
		return result;
	if (answer.GetInt32(sen::key::kStatus, 0) >= 400) {
		printf("the SEN server: %s\n", answer.GetString(sen::key::kDetail, "no relation"));
		return B_ERROR;
	}
	return B_OK;
}
