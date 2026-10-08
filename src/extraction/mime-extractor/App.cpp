/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Alert.h>
#include <Node.h>
#include <String.h>
#include <fs_attr.h>

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


status_t
App::ExtractAttributes(const entry_ref* ref, BMessage* reply)
{
	BNode node(ref);
	status_t result = node.InitCheck();
	if (result != B_OK)
		return result;

	attr_info info;
	result = node.GetAttrInfo(kAttributeInfo, &info);
	if (result == B_ENTRY_NOT_FOUND)
		return B_OK;	// a type without attributes has nothing to contain
	if (result != B_OK)
		return result;

	char* buffer = new char[info.size + 1];
	ssize_t size = node.ReadAttr(kAttributeInfo, B_MESSAGE_TYPE, 0, buffer, info.size);
	BMessage attributes;
	result = size < 0 ? (status_t) size : attributes.Unflatten(buffer);
	delete[] buffer;
	if (result != B_OK)
		return result;

	// one item whose fields have one entry per attribute (the format of the plugin results: it keeps the order)
	BMessage item;
	const char* name;
	for (int32 index = 0; attributes.FindString("attr:name", index, &name) == B_OK; index++) {
		const char* publicName;
		if (attributes.FindString("attr:public_name", index, &publicName) != B_OK || publicName[0] == '\0')
			publicName = name;

		int32 type = 0, width = 0;
		attributes.FindInt32("attr:type", index, &type);
		attributes.FindInt32("attr:width", index, &width);

		item.AddString(sensei::key::kLabel, publicName);
		item.AddString(sensei::key::kName, publicName);
		// what the item is: an attribute of a type (not just a "contains")
		item.AddString(sensei::key::kType, sen::onto::core::mime::kMimeAttribute);
		item.AddString("name", name);
		item.AddString("type", TypeName(type));
		item.AddBool("viewable", attributes.GetBool("attr:viewable", index, false));
		item.AddBool("editable", attributes.GetBool("attr:editable", index, false));
		item.AddBool("searchable", attributes.GetBool("attr:searchable", index, false));
		item.AddInt32("width", width);
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

	message->SendReply(&reply, this);
	Quit();
}


/** for testing: MimeExtractor [--self] <path of the type in the MIME database> */
void
App::ArgvReceived(int32 argc, char** argv)
{
	int32 arg = 1;
	BMessage refs(B_REFS_RECEIVED);
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
