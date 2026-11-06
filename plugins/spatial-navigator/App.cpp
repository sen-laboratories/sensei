/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Entry.h>
#include <LocaleRoster.h>
#include <Message.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <sen/Sen.h>
#include <sen/SenAbout.h>
#include <sen/Sensei.h>

#include "SpatialRelations.h"
#include "SpatialResolver.h"

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.spatial-navigator";


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


/** the language of the system as the code that Wikidata takes (`de`) */
static std::string
SystemLanguage()
{
	BMessage languages;
	const char* code = NULL;
	if (BLocaleRoster::Default()->GetPreferredLanguages(&languages) == B_OK && languages.FindString("language", 0, &code) == B_OK && code != NULL && strlen(code) >= 2) {
		std::string language(code, 2);
		for (char& c : language)
			c = (char)tolower((unsigned char)c);
		return language;
	}
	return "en";
}


/** the work of the plugin: the places that the place of the file is related to, in the way of the relation type, as the answer of the server's question */
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
		spatial::Resolver resolver(SystemLanguage(), fDebug);
		std::vector<spatial::Target> targets;
		result = resolver.Resolve(file, type, &targets);
		for (const spatial::Target& target : targets) {
			BMessage item;
			item.AddRef(sen::key::kTargetRef, &target.ref);
			// the label of the direction: the relation seen from the file that was opened ("within" for the place that it is in, "contains" for a place in it)
			item.AddString(sen::attr::kRelationLabel, spatial::LabelOf(target.relation).c_str());
			item.AddString(sen::attr::kRelationKind, target.fromWikidata ? "P131" : spatial::CodeOf(target.relation).c_str());
			reply.AddMessage(sensei::key::kTargets, &item);
		}
		printf("%s: %zu place(s) %s\n", file.name, targets.size(), type);
	}
	reply.AddInt32(sensei::key::kResult, result);
	message->SendReply(&reply, this);
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


/** for a test from the command line: `SenSpatialNavigator <relation type> <file>` prints what it finds */
void
App::ArgvReceived(int32 argc, char** argv)
{
	if (argc < 2)
		return;
	if (argc != 3) {
		std::cout << "Usage: SenSpatialNavigator <relation type> <place file>\n"
			<< "Prints the places that the place of the file is related to in that way, e.g. relation/x-vnd.sen-labs.relation.spatial.within.\n";
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
	spatial::Resolver resolver(SystemLanguage(), true);
	std::vector<spatial::Target> targets;
	status_t result = resolver.Resolve(file, argv[1], &targets);
	for (const spatial::Target& target : targets)
		printf("  %-10s %s\n", spatial::LabelOf(target.relation).c_str(), target.ref.name);
	if (result != B_OK)
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
	int32 exitCode = app->ExitCode();
	delete app;
	return exitCode;
}
