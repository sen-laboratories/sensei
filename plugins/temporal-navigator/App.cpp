/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "App.h"

#include <Entry.h>
#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <Roster.h>
#include <fs_attr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <sen/Sen.h>
#include <sen/SenAbout.h>
#include <sen/SenAttributes.h>
#include <sen/SenOntoCore.h>
#include <sen/SenOntoTemporal.h>
#include <sen/Sensei.h>

#include "../../common/OpenTarget.h"
#include "TemporalResolver.h"

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.temporal-navigator";


App::App()
	:
	BApplication(kApplicationSignature),
	fExitCode(0),
	fDebug(false)
{
}


void
App::AboutRequested()
{
	sen::ShowAbout();
}


/** the work of the plugin: the files that the file is related to in the way of the relation type, as the answer of the server's question */
void
App::_Resolve(BMessage* message)
{
	BMessage reply(sensei::cmd::kResult);
	entry_ref file;
	const char* type = NULL;
	status_t result = B_OK;
	if (message->FindRef("refs", &file) != B_OK || message->FindString(sen::key::kRelationType, &type) != B_OK) {
		result = B_BAD_VALUE;
	} else {
		temporal::Resolver resolver(fDebug);
		std::vector<temporal::Target> targets;
		result = resolver.Resolve(file, type, &targets);
		// a file that says no time of its own has no temporal relations: that is no error, there is nothing
		if (result == B_ENTRY_NOT_FOUND)
			result = B_OK;
		for (const temporal::Target& target : targets) {
			BMessage item;
			item.AddRef(sen::key::kTargetRef, &target.ref);
			// the label of the direction, seen from the file that was opened ("before" for what comes later, "after" for what came earlier)
			item.AddString(sen::attr::kRelationLabel, temporal::LabelOf(target.relation).c_str());
			item.AddString(sen::onto::core::attr::kRelProvenance, kApplicationSignature);
			item.AddString(sen::attr::kRelationKind, temporal::LabelOf(target.relation).c_str());
			item.AddString(sen::onto::temporal::attr::kAllen, temporal::CodeOf(target.relation).c_str());
			reply.AddMessage(sensei::key::kTargets, &item);
		}
		printf("%s: %zu file(s) %s\n", file.name, targets.size(), type);
	}
	reply.AddInt32(sensei::key::kResult, result);
	message->SendReply(&reply, this);
}


/** The thing that a file stands for: the file of a relation (double-clicked in a relation folder) has its target in SEN:REL:TRG; a file with no such attribute is the thing itself. */
static bool
TargetOf(const entry_ref& file, entry_ref* target)
{
	BNode node(&file);
	attr_info info;
	if (node.InitCheck() != B_OK)
		return false;
	if (node.GetAttrInfo(sen::attr::kRelationTargetRef, &info) != B_OK) {
		*target = file;
		return true;
	}
	if (info.type != B_REF_TYPE || info.size <= 0)
		return false;
	bool found = false;
	char* buffer = new char[info.size];
	if (node.ReadAttr(sen::attr::kRelationTargetRef, B_REF_TYPE, 0, buffer, info.size) == info.size) {
		BMessage holder;
		entry_ref stored;
		if (holder.AddData("ref", B_REF_TYPE, buffer, info.size, false) == B_OK && holder.FindRef("ref", &stored) == B_OK && BEntry(&stored).Exists()) {
			*target = stored;
			found = true;
		}
	}
	delete[] buffer;
	return found;
}


void
App::RefsReceived(BMessage* message)
{
	// this plugin is the application of the temporal relation types (the server finds it by that), so a double click on the file of a relation comes here. It has nothing of its own to
	// show: the event is opened with the application of its type (Shoji), as a double click on it would
	entry_ref file, target;
	for (int32 index = 0; message->FindRef("refs", index, &file) == B_OK; index++) {
		if (!TargetOf(file, &target)) {
			fprintf(stderr, "%s: nothing to open (the target of the relation is gone).\n", file.name);
			continue;
		}
		status_t result = sensei::OpenTarget(target);
		if (result != B_OK)
			fprintf(stderr, "could not open %s: %s\n", target.name, strerror(result));
	}
	Quit();
}


void
App::MessageReceived(BMessage* message)
{
	if (message->what == sensei::cmd::kResolve) {
		_Resolve(message);
		Quit();		// (a plugin is a multiple launch application that ends when it has answered)
		return;
	}
	BApplication::MessageReceived(message);
}


/** for a test from the command line: `SenTemporalNavigator <relation type> <file>` prints what it finds */
void
App::ArgvReceived(int32 argc, char** argv)
{
	if (argc < 2)
		return;
	if (argc != 3) {
		std::cout << "Usage: SenTemporalNavigator <relation type> <file>\n"
			<< "Prints the files that the file is related to in that way, e.g. relation/x-vnd.sen-labs.relation.temporal.overlaps.\n";
		Quit();
		return;
	}
	fDebug = true;
	entry_ref file;
	if (get_ref_for_path(argv[2], &file) != B_OK) {
		std::cerr << argv[2] << ": no such file" << std::endl;
		fExitCode = 1;
		Quit();
		return;
	}
	temporal::Resolver resolver(true);
	std::vector<temporal::Target> targets;
	status_t result = resolver.Resolve(file, argv[1], &targets);
	if (result == B_ENTRY_NOT_FOUND)
		std::cout << file.name << " says no time of its own.\n";
	for (const temporal::Target& target : targets)
		printf("  %-14s %s\n", temporal::LabelOf(target.relation).c_str(), target.ref.name);
	if (result != B_OK && result != B_ENTRY_NOT_FOUND)
		fExitCode = 1;
	Quit();
}


int
main()
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	App* app = new App();
	if (app->InitCheck() != B_OK) {
		delete app;
		return 1;
	}
	app->Run();
	const int32 code = app->ExitCode();
	delete app;
	return code;
}
