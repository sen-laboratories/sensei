/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Entry.h>
#include <LocaleRoster.h>
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
#include <sen/SenOntoSpatial.h>
#include <sen/Sensei.h>

#include "SpatialRelations.h"
#include "SpatialResolver.h"

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.spatial-navigator";
/** the source of a relation that Wikidata says: the property P131, "located in the administrative territorial entity" */
static const char* kWikidataLocatedIn = "https://www.wikidata.org/wiki/Property:P131";
/** the source of what is nearby: OpenStreetMap, asked through the Overpass API */
static const char* kOverpassApi = "https://wiki.openstreetmap.org/wiki/Overpass_API";


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
		BMessage libraries;
		message->FindMessage(sensei::key::kLibraries, &libraries);
		spatial::Resolver resolver(SystemLanguage(), fDebug, &libraries);
		std::vector<spatial::Target> targets;
		result = resolver.Resolve(file, type, &targets);
		for (const spatial::Target& target : targets) {
			BMessage item;
			item.AddRef(sen::key::kTargetRef, &target.ref);
			// the label of the direction: the relation seen from the file that was opened ("within" for the place that it is in, "contains" for a place in it)
			// (a nearby thing says what it is, and how far: the label of a direction would be the same for all of them)
			item.AddString(sen::attr::kRelationLabel, target.nearby ? target.label.c_str() : spatial::LabelOf(target.relation).c_str());
			if (target.nearby && target.distanceMetres >= 0)
				item.AddInt32(sen::onto::spatial::attr::kDistance, target.distanceMetres);
			// what it is: in words for people (Kind) and the code of the Region Connection Calculus that the shapes gave (Rcc8). A place that Wikidata says contains this one has neither: no
			// shapes were compared, "located in" says nothing about the border. Where the relation comes from is its provenance: this plugin worked it out from the shapes, or Wikidata said it
			// (the property "located in the administrative territorial entity")
			if (target.nearby) {
				// (a named thing in the area: no shapes were compared; it is where OpenStreetMap says it is)
				item.AddString(sen::onto::core::attr::kRelProvenance, kOverpassApi);
			} else if (target.fromWikidata) {
				item.AddString(sen::onto::core::attr::kRelProvenance, kWikidataLocatedIn);
			} else {
				item.AddString(sen::onto::core::attr::kRelProvenance, kApplicationSignature);
				item.AddString(sen::attr::kRelationKind, spatial::KindOf(target.relation).c_str());
				item.AddString(sen::attr::kRelationRcc8, spatial::CodeOf(target.relation).c_str());
			}
			reply.AddMessage(sensei::key::kTargets, &item);
		}
		printf("%s: %zu place(s) %s\n", file.name, targets.size(), type);

		// the places that were made (the parents that were missing) have no picture, outline or address: Spaces enriches them in the background, one after the other, as the server queues it.
		// What they have is kept; the settings of Spaces (picture, outline) decide the rest; nothing is asked of the user (auto)
		if (!resolver.Made().empty()) {
			BMessenger server(sen::kServerSignature);
			if (server.IsValid()) {
				BMessage enrich(sen::cmd::kEnrichRefs);
				for (const entry_ref& made : resolver.Made())
					enrich.AddRef("refs", &made);
				enrich.AddBool("wipe", false);
				enrich.AddBool("auto", true);
				server.SendMessage(&enrich);
			}
		}
	}
	reply.AddInt32(sensei::key::kResult, result);
	message->SendReply(&reply, this);
}


/**
 * The place that a file stands for. The file of a relation (double-clicked in a relation folder) has its target in SEN:REL:TRG (a ref as a message holds it); false if the place is gone.
 * A file with no such attribute is the place itself (an item of a menu hands over the target, which is the place).
 */
static bool
PlaceOf(const entry_ref& file, entry_ref* place)
{
	BNode node(&file);
	attr_info info;
	if (node.InitCheck() != B_OK)
		return false;
	if (node.GetAttrInfo(sen::attr::kRelationTargetRef, &info) != B_OK) {
		*place = file;
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
			*place = stored;
			found = true;
		}
	}
	delete[] buffer;
	return found;
}


void
App::RefsReceived(BMessage* message)
{
	// this plugin is the application of the spatial relation types (the server finds it by that), so a double click on the file of a relation, and a click on a place in the menu of a relation,
	// come here. It has nothing of its own to show: the place is opened with the application of its type (Shoji), as a double click on the place would
	entry_ref file, place;
	for (int32 index = 0; message->FindRef("refs", index, &file) == B_OK; index++) {
		if (!PlaceOf(file, &place)) {
			fprintf(stderr, "%s: no place to open (the place of the relation is gone).\n", file.name);
			continue;
		}
		status_t result = be_roster->Launch(&place);
		if (result != B_OK && result != B_ALREADY_RUNNING)
			fprintf(stderr, "could not open %s: %s\n", place.name, strerror(result));
	}
	Quit();
}


void
App::MessageReceived(BMessage* message)
{
	// the settings window of SEN: the instance that is started for it ends with the session
	if (fSession.Handle(message, &fSettingsProvider, kApplicationSignature)) {
		if (fSession.IsEnded())
			Quit();
		else
			SetPulseRate(5000000);
		return;
	}
	if (message->what == sensei::cmd::kResolve) {
		_Resolve(message);
		Quit();		// (a plugin is a multiple launch application that ends when it has answered)
		return;
	}
	BApplication::MessageReceived(message);
}


void
App::Pulse()
{
	if (fSession.Expired())
		Quit();
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
		if (target.nearby)
			printf("  %-10s %6d m  %s  (%s)\n", "nearby", target.distanceMetres, target.ref.name, target.label.c_str());
		else
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
