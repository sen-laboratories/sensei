/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Alert.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <MimeType.h>
#include <Node.h>
#include <Path.h>
#include <Roster.h>
#include <fs_attr.h>

#include <stdio.h>

#include <sen/Sen.h>
#include <sen/SenOntoCore.h>

static const char* kApplicationSignature = "application/x-vnd.sen-labs.ReferenceNavigator";
// the list of the attributes in the window of FileTypes, the one that shows the types
static const char* kAttributeListView = "listview attr";


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


/** the MIME type that a file in the MIME database stands for ("entity/x-vnd..."), or empty for any other file */
static BString
MimeTypeOfDatabaseEntry(const entry_ref& ref)
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

	path.Remove(0, database.Length());
	return path;
}


/**
 * The file of a relation in a relation view is a proxy for the target of the relation, which it carries as SEN:REL:TRG: what
 * is opened is the target. Any other file (and a folder, which is a nested relation) is the target itself.
 */
static entry_ref
ResolveProxy(const entry_ref& ref)
{
	BEntry entry(&ref);
	BNode node(&ref);
	attr_info info;
	if (entry.InitCheck() != B_OK || entry.IsDirectory() || node.InitCheck() != B_OK
			|| node.GetAttrInfo(sen::attr::kRelationTargetRef, &info) != B_OK || info.type != B_REF_TYPE || info.size <= 0)
		return ref;

	entry_ref target = ref;
	char* buffer = new char[info.size];
	if (node.ReadAttr(sen::attr::kRelationTargetRef, B_REF_TYPE, 0, buffer, info.size) == info.size) {
		// the ref is stored as a message holds it
		BMessage holder;
		entry_ref found;
		if (holder.AddData("ref", B_REF_TYPE, buffer, info.size, false) == B_OK && holder.FindRef("ref", &found) == B_OK
				&& BEntry(&found).Exists())
			target = found;
	}
	delete[] buffer;
	return target;
}


/** the attribute that is meant: named in the properties of the relation of the message, or on the file of the relation */
static BString
AttributeNameOf(const BMessage* message, const entry_ref& ref)
{
	BString name;
	BMessage properties;
	if (message->FindMessage(sen::key::kRelationProperties, &properties) == B_OK
			&& properties.FindString(sen::onto::core::attr::kAttrName, &name) == B_OK)
		return name;

	BNode node(&ref);
	if (node.InitCheck() == B_OK && node.ReadAttrString(sen::onto::core::attr::kAttrName, &name) == B_OK)
		return name;
	return BString();
}


/** the position of an attribute in the attribute info of a type (the position in the list of FileTypes), or -1 */
static int32
IndexOfAttribute(const BString& mimeType, const BString& attribute)
{
	BMimeType type(mimeType.String());
	BMessage info;
	if (!type.IsValid() || type.GetAttrInfo(&info) != B_OK)
		return -1;

	const char* name;
	for (int32 index = 0; info.FindString("attr:name", index, &name) == B_OK; index++) {
		if (attribute == name)
			return index;
	}
	return -1;
}


/** a message to the list of attributes in a window of FileTypes: "<property> [index] of View <name> of Window <window>" */
static BMessage
AttributeListMessage(uint32 what, const char* property, int32 index, int32 window)
{
	BMessage message(what);
	if (index >= 0)
		message.AddSpecifier(property, index);
	else
		message.AddSpecifier(property);
	message.AddSpecifier("View", kAttributeListView);
	message.AddSpecifier("Window", window);
	return message;
}


/**
 * The window of FileTypes that has the list of attributes, and the number of attributes in it (0 while there is none, or no window
 * yet). FileTypes has other windows (a file panel is the first one), so the window is not known: it is the one that has the list.
 */
static int32
ListedAttributes(const BMessenger& fileTypes, int32* window)
{
	BMessage windows(B_COUNT_PROPERTIES);
	windows.AddSpecifier("Window");
	BMessage reply;
	if (fileTypes.SendMessage(&windows, &reply, 2000000, 2000000) != B_OK || reply.what == B_MESSAGE_NOT_UNDERSTOOD)
		return 0;
	int32 windowCount = reply.GetInt32("result", 0);

	for (int32 index = 0; index < windowCount; index++) {
		BMessage count = AttributeListMessage(B_COUNT_PROPERTIES, "Item", -1, index);
		BMessage countReply;
		if (fileTypes.SendMessage(&count, &countReply, 2000000, 2000000) != B_OK
				|| countReply.what == B_MESSAGE_NOT_UNDERSTOOD || countReply.GetInt32("error", B_OK) != B_OK)
			continue;	// not this window: it has no such list
		*window = index;
		return countReply.GetInt32("result", 0);
	}
	return 0;
}


status_t
App::OpenMimeType(const BString& mimeType, const BString& attribute)
{
	// which type holds the attribute: the type itself, else its supertype (the MIME database does not inherit them)
	BString owner(mimeType);
	int32 index = -1;
	if (!attribute.IsEmpty()) {
		index = IndexOfAttribute(owner, attribute);
		int32 slash = mimeType.FindFirst('/');
		if (index < 0 && slash > 0) {
			BString supertype;
			mimeType.CopyInto(supertype, 0, slash);
			index = IndexOfAttribute(supertype, attribute);
			if (index >= 0)
				owner = supertype;
		}
	}

	// The FileTypes of SEN (found by its signature) is asked by a message, and decides itself what to do: it shows the type, closes
	// the dialog of any other attribute and opens one for this.
	entry_ref senFileTypes;
	if (be_roster->FindApp(sen::kFileTypesSignature, &senFileTypes) == B_OK) {
		BMessenger messenger(sen::kFileTypesSignature);
		if (!messenger.IsValid()) {
			status_t launched = be_roster->Launch(sen::kFileTypesSignature);
			if (launched != B_OK && launched != B_ALREADY_RUNNING)
				return launched;
			messenger = BMessenger(sen::kFileTypesSignature);
			for (int tries = 0; tries < 50 && !messenger.IsValid(); tries++) {
				snooze(100000);
				messenger = BMessenger(sen::kFileTypesSignature);
			}
		}

		BMessage open(sen::cmd::kOpenMimeAttribute);
		open.AddString(sen::key::kMimeType, owner);
		if (!attribute.IsEmpty())
			open.AddString(sen::key::kAttributeName, attribute);
		return messenger.SendMessage(&open);
	}

	// The FileTypes of Haiku (single launch, it needs the MIME database for itself) shows the type that is asked for (-type) when
	// it is started; one that runs is only brought to the front (the list of its window is that of another type). For the attribute
	// the application is scripted with what every application has.
	bool running = BMessenger(sen::kHaikuFileTypesSignature).IsValid();

	// (the roster puts the program in front of the arguments itself)
	const char* arguments[] = {"-type", owner.String()};
	status_t result = be_roster->Launch(sen::kHaikuFileTypesSignature, 2, arguments);
	if (result == B_ALREADY_RUNNING)
		result = B_OK;
	if (result != B_OK || index < 0 || running)
		return result;

	// the window needs a moment to be there with the attributes of the type
	BMessenger fileTypes(sen::kHaikuFileTypesSignature);
	int32 window = 0;
	for (int tries = 0; tries < 60 && ListedAttributes(fileTypes, &window) <= index; tries++)
		snooze(100000);
	if (ListedAttributes(fileTypes, &window) <= index)
		return B_TIMED_OUT;

	// like a double click on the attribute: its window opens
	BMessage invoke = AttributeListMessage(B_EXECUTE_PROPERTY, "Item", index, window);
	BMessage reply;
	result = fileTypes.SendMessage(&invoke, &reply, 2000000, 2000000);
	if (result == B_OK && reply.what == B_MESSAGE_NOT_UNDERSTOOD)
		result = reply.GetInt32("error", B_ERROR);
	return result;
}



void
App::RefsReceived(BMessage* message)
{
	entry_ref ref;
	status_t result = message->FindRef("refs", &ref);

	if (result == B_OK) {
		// what is meant (an attribute) is on the proxy and in the properties, the target is what is opened
		BString attribute = AttributeNameOf(message, ref);
		ref = ResolveProxy(ref);

		BString mimeType = MimeTypeOfDatabaseEntry(ref);
		if (!mimeType.IsEmpty())
			result = OpenMimeType(mimeType, attribute);
		else
			result = be_roster->Launch(&ref);	// any other target: opened as usual
	}

	if (result != B_OK) {
		BString error("Could not open the target: ");
		error << strerror(result);
		BAlert* alert = new BAlert("SEN MIME Type Navigator", error.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL,
			B_STOP_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}

	Quit();
}


/** for testing: ReferenceNavigator <path of the type in the MIME database or of any file> [<name of an attribute>] */
void
App::ArgvReceived(int32 argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <target> [<attribute>]\n", argv[0]);
		return;
	}

	BEntry entry(argv[1]);
	entry_ref ref;
	if (entry.GetRef(&ref) != B_OK)
		return;

	BMessage refs(B_REFS_RECEIVED);
	refs.AddRef("refs", &ref);
	if (argc > 2) {
		BMessage properties;
		properties.AddString(sen::onto::core::attr::kAttrName, argv[2]);
		refs.AddMessage(sen::key::kRelationProperties, &properties);
	}
	RefsReceived(&refs);
}
