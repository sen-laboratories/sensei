/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file TargetOpener.h
 * @brief The application of a navigator that opens the target of a relation with its own application (header only).
 *
 * Navigators are named for the relation type that they handle (AuthorshipNavigator, KnowsNavigator, DocumentReference...). Those whose target is
 * simply opened by the application that is set for its type (a person in Shoji) are this program with another signature and name:
 *
 *     class App : public sensei::TargetOpenerApp {
 *     public:
 *         App() : TargetOpenerApp("application/x-vnd.sen-plugin.sen-labs.authorship-navigator", "SEN Authorship Navigator") {}
 *     };
 *
 * It is started with the file of a relation (in a relation view that is a proxy for the target, see RelationTarget.h) or with the target itself (an
 * item of a menu) and opens the target the way a double click on it would. Properties of the relation that a target application needs as arguments
 * are for a navigator of its own (the places in a document for DocumentReference).
 */

#include <Alert.h>
#include <Application.h>
#include <Entry.h>
#include <Roster.h>
#include <String.h>

#include <stdio.h>
#include <string.h>

#include <sen/SenAbout.h>

#include "RelationTarget.h"

namespace sensei {

class TargetOpenerApp : public BApplication {
public:
	/**
	 * @param signature the signature of the navigator
	 * @param title the name of the navigator, the title of the message when something goes wrong
	 */
	TargetOpenerApp(const char* signature, const char* title)
		:
		BApplication(signature),
		fTitle(title)
	{
	}

	virtual void
	AboutRequested()
	{
		sen::ShowAbout();
	}

	virtual void
	RefsReceived(BMessage* message)
	{
		status_t result = B_OK;
		entry_ref ref;

		for (int32 index = 0; message->FindRef("refs", index, &ref) == B_OK; index++) {
			bool found;
			entry_ref target = RelationTarget(ref, &found);
			if (!found) {
				result = B_ENTRY_NOT_FOUND;
				break;
			}

			// the target is opened by the application of its type
			result = be_roster->Launch(&target);
			if (result != B_OK && result != B_ALREADY_RUNNING)
				break;
			result = B_OK;
		}

		if (result != B_OK) {
			BString error(result == B_ENTRY_NOT_FOUND
				? "The file of this relation is gone: the relation points to a file that does not exist any more."
				: "Could not open the target: ");
			if (result != B_ENTRY_NOT_FOUND)
				error << strerror(result);
			BAlert* alert = new BAlert(fTitle.String(), error.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
		Quit();
	}

	/** for testing: the files of relations (or the targets) as arguments */
	virtual void
	ArgvReceived(int32 argc, char** argv)
	{
		BMessage refs(B_REFS_RECEIVED);
		for (int32 index = 1; index < argc; index++) {
			entry_ref ref;
			if (get_ref_for_path(argv[index], &ref) == B_OK)
				refs.AddRef("refs", &ref);
		}
		RefsReceived(&refs);
	}

private:
	BString	fTitle;
};

}	// namespace sensei
