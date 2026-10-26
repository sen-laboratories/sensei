/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"
#include <sen/SenAbout.h>

#include <Alert.h>
#include <Entry.h>
#include <Messenger.h>
#include <Node.h>
#include <Roster.h>
#include <fs_attr.h>

#include <stdio.h>

#include <sen/Sen.h>

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.relation-navigator";
static const char* kTrackerSignature = "application/x-vnd.Be-TRAK";


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


entry_ref
App::TargetOf(const entry_ref& ref, bool* found)
{
	*found = true;

	BEntry entry(&ref);
	BNode node(&ref);
	attr_info info;
	if (entry.InitCheck() != B_OK || entry.IsDirectory() || node.InitCheck() != B_OK
			|| node.GetAttrInfo(sen::attr::kRelationTargetRef, &info) != B_OK || info.type != B_REF_TYPE || info.size <= 0)
		return ref;	// not the file of a relation: what is opened is the file itself

	entry_ref target = ref;
	*found = false;
	char* buffer = new char[info.size];
	if (node.ReadAttr(sen::attr::kRelationTargetRef, B_REF_TYPE, 0, buffer, info.size) == info.size) {
		// the ref is stored as a message holds it
		BMessage holder;
		entry_ref stored;
		if (holder.AddData("ref", B_REF_TYPE, buffer, info.size, false) == B_OK && holder.FindRef("ref", &stored) == B_OK
				&& BEntry(&stored).Exists()) {
			target = stored;
			*found = true;
		}
	}
	delete[] buffer;
	return target;
}


status_t
App::Reveal(const entry_ref& target)
{
	BEntry entry(&target, true);
	status_t result = entry.InitCheck();
	if (result != B_OK)
		return result;

	BMessage open(B_REFS_RECEIVED);
	if (entry.IsDirectory()) {
		// a folder is opened, like a double-click on it
		entry_ref folder;
		entry.GetRef(&folder);
		open.AddRef("refs", &folder);
	} else {
		// the folder of the file, with the file selected (Tracker does this for the files that are in the data: nodeRefToSelect)
		BEntry parent;
		entry_ref parentRef;
		node_ref node;
		if ((result = entry.GetParent(&parent)) != B_OK || (result = parent.GetRef(&parentRef)) != B_OK
				|| (result = entry.GetNodeRef(&node)) != B_OK)
			return result;
		open.AddRef("refs", &parentRef);
		open.AddData("nodeRefToSelect", B_RAW_TYPE, &node, sizeof(node_ref));
	}

	BMessenger tracker(kTrackerSignature);
	if (!tracker.IsValid())
		return B_NOT_INITIALIZED;
	return tracker.SendMessage(&open);
}


void
App::RefsReceived(BMessage* message)
{
	entry_ref ref;
	status_t result = B_OK;
	bool anyTarget = false;

	for (int32 index = 0; message->FindRef("refs", index, &ref) == B_OK; index++) {
		bool found;
		entry_ref target = TargetOf(ref, &found);
		if (!found) {
			BAlert* alert = new BAlert("SEN Relation Navigator",
				"The file of this relation is gone: the relation points to a file that does not exist any more.", "OK", NULL,
				NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			continue;
		}
		anyTarget = true;
		result = Reveal(target);
		if (result != B_OK) {
			fprintf(stderr, "could not show %s in Tracker: %s\n", target.name, strerror(result));
		}
	}

	if (!anyTarget && result == B_OK) {
		// nothing was shown: the message had no file
		fprintf(stderr, "no relation file to open.\n");
	}
	Quit();
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	// for testing: the files of relations as arguments
	BMessage refs(B_REFS_RECEIVED);
	for (int32 index = 1; index < argc; index++) {
		entry_ref ref;
		if (get_ref_for_path(argv[index], &ref) == B_OK)
			refs.AddRef("refs", &ref);
	}
	RefsReceived(&refs);
}


void
App::AboutRequested()
{
	sen::ShowAbout();
}
