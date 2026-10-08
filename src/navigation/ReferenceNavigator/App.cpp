/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Alert.h>
#include <Entry.h>
#include <Node.h>
#include <fs_attr.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <stdio.h>

#include <sen/Sen.h>

static const char* kApplicationSignature = "application/x-vnd.sen-labs.ReferenceNavigator";
static const char* kFileTypesSignature = "application/x-vnd.Haiku-FileTypes";


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


void
App::RefsReceived(BMessage* message)
{
	entry_ref ref;
	status_t result = message->FindRef("refs", &ref);

	if (result == B_OK) {
		ref = ResolveProxy(ref);
		BString mimeType = MimeTypeOfDatabaseEntry(ref);
		if (!mimeType.IsEmpty()) {
			// a type of the MIME database: FileTypes shows it (-type selects it; an instance that runs just comes to the front)
			const char* arguments[] = {"FileTypes", "-type", mimeType.String()};
			result = be_roster->Launch(kFileTypesSignature, 3, arguments);
			if (result == B_ALREADY_RUNNING)
				result = B_OK;
		} else {
			// any other target: opened as usual
			result = be_roster->Launch(&ref);
		}
	}

	if (result != B_OK) {
		BString error("Could not open the target of the reference: ");
		error << strerror(result);
		BAlert* alert = new BAlert("SEN Reference Navigator", error.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL,
			B_STOP_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}

	Quit();
}


/** for testing: ReferenceNavigator <path of the target> */
void
App::ArgvReceived(int32 argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <target>\n", argv[0]);
		return;
	}

	BEntry entry(argv[1]);
	entry_ref ref;
	if (entry.GetRef(&ref) != B_OK)
		return;

	BMessage refs(B_REFS_RECEIVED);
	refs.AddRef("refs", &ref);
	RefsReceived(&refs);
}
