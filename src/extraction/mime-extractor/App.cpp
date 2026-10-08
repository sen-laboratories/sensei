/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Alert.h>
#include <Node.h>
#include <String.h>
#include <FindDirectory.h>
#include <Path.h>
#include <fs_attr.h>

#include <set>
#include <string>

#include <stdio.h>
#include <string.h>

#include <sen/Sen.h>
#include <sen/SenOntoCore.h>
#include <sen/Sensei.h>

static const char* kApplicationSignature = "application/x-vnd.sen-labs.MimeExtractor";
static const char* kAttributeInfo = "META:ATTR_INFO";


App::App()
	:
	BApplication(kApplicationSignature)
{
}


int
main()
{
	App app;
	app.Run();
	return 0;
}


/** a name for the type of an attribute (what people say, not the code): string, int32, bool, ... */
static BString
TypeName(int32 type)
{
	switch ((uint32) type) {
		case B_STRING_TYPE:		return "string";
		case B_INT32_TYPE:		return "int32";
		case B_INT16_TYPE:		return "int16";
		case B_INT64_TYPE:		return "int64";
		case B_INT8_TYPE:		return "int8";
		case B_UINT32_TYPE:		return "uint32";
		case B_BOOL_TYPE:		return "bool";
		case B_TIME_TYPE:		return "time";
		case B_FLOAT_TYPE:		return "float";
		case B_DOUBLE_TYPE:		return "double";
		case B_REF_TYPE:		return "ref";
		case B_MESSAGE_TYPE:	return "message";
		case B_MIME_STRING_TYPE: return "mime type";
		case B_VECTOR_ICON_TYPE: return "icon";
	}

	// anything else: the four characters of the code
	char code[5] = {(char) (type >> 24), (char) (type >> 16), (char) (type >> 8), (char) type, 0};
	return BString(code);
}


/** the attribute info of the entry of a type in the MIME database (empty if it has none) */
static status_t
ReadAttributeInfo(BNode& node, BMessage* attributes)
{
	attr_info info;
	status_t result = node.GetAttrInfo(kAttributeInfo, &info);
	if (result == B_ENTRY_NOT_FOUND)
		return B_OK;
	if (result != B_OK)
		return result;

	char* buffer = new char[info.size + 1];
	ssize_t size = node.ReadAttr(kAttributeInfo, B_MESSAGE_TYPE, 0, buffer, info.size);
	result = size < 0 ? (status_t) size : attributes->Unflatten(buffer);
	delete[] buffer;
	return result;
}


/** the supertype of the type that an entry of the MIME database stands for (entity/x-vnd... -> entity), or empty */
static BString
SupertypeEntry(const entry_ref& ref, BPath* supertypePath)
{
	BPath settings, entry(&ref);
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) != B_OK || settings.Append("mime_db") != B_OK
			|| entry.InitCheck() != B_OK)
		return BString();

	BString database(settings.Path());
	database << "/";
	BString path(entry.Path());
	if (!path.StartsWith(database))
		return BString();

	// the type is the rest of the path: the supertype is its first part
	path.Remove(0, database.Length());
	int32 slash = path.FindFirst('/');
	if (slash <= 0)
		return BString();	// a supertype itself

	BString supertype;
	path.CopyInto(supertype, 0, slash);
	supertypePath->SetTo(settings.Path(), supertype.String());	// settings is the MIME database by now
	return supertype;
}


/** add the attributes of the info as entries of the item, those of a name that is there already (seen) not */
static void
AddAttributes(const BMessage& attributes, BMessage* item, std::set<std::string>* seen)
{
	const char* name;
	for (int32 index = 0; attributes.FindString("attr:name", index, &name) == B_OK; index++) {
		if (!seen->insert(name).second)
			continue;

		const char* publicName;
		if (attributes.FindString("attr:public_name", index, &publicName) != B_OK || publicName[0] == '\0')
			publicName = name;

		int32 type = 0, width = 0;
		attributes.FindInt32("attr:type", index, &type);
		attributes.FindInt32("attr:width", index, &width);

		item->AddString(sensei::key::kLabel, publicName);
		item->AddString(sensei::key::kName, publicName);
		// what the item is: an attribute of a type (not just a "contains")
		item->AddString(sensei::key::kType, sen::onto::core::mime::kMimeAttribute);
		item->AddString("name", name);
		item->AddString("type", TypeName(type));
		item->AddBool("viewable", attributes.GetBool("attr:viewable", index, false));
		item->AddBool("editable", attributes.GetBool("attr:editable", index, false));
		item->AddBool("searchable", attributes.GetBool("attr:searchable", index, false));
		item->AddInt32("width", width);
	}
}


status_t
App::ExtractAttributes(const entry_ref* ref, BMessage* reply)
{
	BNode node(ref);
	status_t result = node.InitCheck();
	if (result != B_OK)
		return result;

	BMessage attributes;
	result = ReadAttributeInfo(node, &attributes);
	if (result != B_OK)
		return result;

	// one item whose fields have one entry per attribute (the format of the plugin results: it keeps the order)
	BMessage item;
	std::set<std::string> seen;
	AddAttributes(attributes, &item, &seen);

	// the attributes that the type has from its supertype (the MIME database does not inherit them, the Attributes menu of
	// Tracker shows both): after its own
	BPath supertypePath;
	if (!SupertypeEntry(*ref, &supertypePath).IsEmpty()) {
		BNode supertypeNode(supertypePath.Path());
		BMessage inherited;
		if (supertypeNode.InitCheck() == B_OK && ReadAttributeInfo(supertypeNode, &inherited) == B_OK)
			AddAttributes(inherited, &item, &seen);
	}

	if (!item.IsEmpty())
		reply->AddMessage(sensei::key::kItem, &item);
	return B_OK;
}


void
App::RefsReceived(BMessage* message)
{
	entry_ref ref;
	if (message->FindRef("refs", &ref) != B_OK) {
		BAlert* alert = new BAlert("Error launching SEN MIME Extractor", "Failed to resolve the MIME type.", "Oh no.");
		alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
		alert->Go();
		Quit();
		return;
	}

	BMessage reply(sensei::cmd::kResult);
	status_t result = ExtractAttributes(&ref, &reply);

	// the result of the plugin is an int32 status_t (the server reads it), with a text for people and logs
	reply.AddInt32(sensei::key::kResult, result);
	reply.AddString(sen::key::kDetail, strerror(result));

	if (message->GetBool("print", false))
		reply.PrintToStream();
	message->SendReply(&reply, this);
	Quit();
}


/** for testing: MimeExtractor [--self] <path of the type in the MIME database> */
void
App::ArgvReceived(int32 argc, char** argv)
{
	int32 arg = 1;
	BMessage refs(B_REFS_RECEIVED);
	refs.AddBool("print", true);
	if (arg < argc && strncmp(argv[arg], sensei::kOptionSelf, strlen(sensei::kOptionSelf)) == 0) {
		refs.AddBool(sen::conf::kSelf, true);
		arg++;
	}
	if (arg >= argc) {
		fprintf(stderr, "usage: %s [--self] <entry of the MIME type>\n", argv[0]);
		return;
	}

	BEntry entry(argv[arg]);
	entry_ref ref;
	if (entry.GetRef(&ref) == B_OK) {
		refs.AddRef("refs", &ref);
		RefsReceived(&refs);
	}
}
