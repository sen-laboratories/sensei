/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"
#include <sen/SenAbout.h>

#include <Alert.h>
#include <Entry.h>
#include <Roster.h>
#include <String.h>

#include <stdio.h>

#include "../../common/RelationTarget.h"

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.authorship-navigator";


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


void
App::RefsReceived(BMessage* message)
{
	status_t result = B_OK;
	entry_ref ref;

	for (int32 index = 0; message->FindRef("refs", index, &ref) == B_OK; index++) {
		bool found;
		entry_ref author = sensei::RelationTarget(ref, &found);
		if (!found) {
			result = B_ENTRY_NOT_FOUND;
			break;
		}

		// the author is opened by the application of the type of the file
		result = be_roster->Launch(&author);
		if (result != B_OK && result != B_ALREADY_RUNNING)
			break;
		result = B_OK;
	}

	if (result != B_OK) {
		BString error(result == B_ENTRY_NOT_FOUND ? "The file of this author is gone: the relation points to a file that does not exist any more."
			: "Could not open the author: ");
		if (result != B_ENTRY_NOT_FOUND)
			error << strerror(result);
		BAlert* alert = new BAlert("SEN Authorship Navigator", error.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}
	Quit();
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	// for testing: the files of relations (or of people) as arguments
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
