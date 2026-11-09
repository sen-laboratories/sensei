/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Entry.h>
#include <Node.h>
#include <String.h>
#include <fs_attr.h>

#include <iostream>
#include <stdio.h>
#include <string.h>

#include <sen/Sen.h>
#include <sen/SenAttributes.h>
#include <sen/SenOntoPeople.h>
#include <sen/SenPluginSettings.h>
#include <sen/Sensei.h>

#include "../../common/EnrichOnDemand.h"
#include "../../common/Notice.h"
#include "../../common/PersonStatus.h"

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.personal-relation-navigator";
static const char* kRobinSignature = "application/x-vnd.sen-plugin.sen-labs.person-enricher";


App::App()
	:
	TargetOpenerApp(kApplicationSignature, "SEN Personal Relation Navigator"),
	fDebug(false),
	fExitCode(0)
{
}


void
App::MessageReceived(BMessage* message)
{
	if (message->what == sensei::cmd::kResolve) {
		_Resolve(message);
		Quit();		// (a plugin is a multiple launch application that ends when it has answered)
		return;
	}
	TargetOpenerApp::MessageReceived(message);
}


void
App::_Resolve(BMessage* message)
{
	BMessage reply(sensei::cmd::kResult);
	entry_ref file;
	const char* type = NULL;
	status_t result = B_OK;

	if (message->FindRef("refs", &file) != B_OK || message->FindString(sen::key::kRelationType, &type) != B_OK) {
		result = B_BAD_VALUE;
	} else if (strcmp(type, sen::onto::people::mime::kStatusRelation) == 0) {
		std::vector<Status> statuses;
		_ResolveStatus(file, &statuses);
		for (const Status& status : statuses) {
			BMessage item;
			item.AddRef(sen::key::kTargetRef, &status.ref);
			item.AddString(sen::attr::kRelationLabel, status.title.c_str());
			reply.AddMessage(sensei::key::kTargets, &item);
		}
	}

	reply.AddInt32(sensei::key::kResult, result);
	message->SendReply(&reply, this);
}


/**
 * The posts of a person, as the files that the relation shows (Robin writes them: see common/PersonStatus.h). When they were fetched more than an hour ago Robin is asked to fetch them again, from
 * the feed of the person or the accounts that its links name, and a machine that is offline shows the posts that it has. A person that Robin has not looked at yet, and that has no feed, no
 * links and no homepage, has nothing to ask: with the setting *Enrich automatically on demand* Robin enriches it now, else the user is told.
 */
void
App::_ResolveStatus(const entry_ref& file, std::vector<Status>* statuses)
{
	BNode node(&file);
	if (node.InitCheck() != B_OK)
		return;
	auto read = [&node](const char* attribute) {
		BString value;
		return node.ReadAttrString(attribute, &value) == B_OK ? std::string(value.String()) : std::string();
	};
	BString name(read(sen::onto::people::attr::kName).c_str());
	if (name.IsEmpty())
		name = file.name;
	// something that posts could come from: a feed, links to accounts, a homepage (which may name a feed)
	auto hasSource = [&]() {
		return !read(sen::onto::people::attr::kFeed).empty() || !read("schema:feed").empty() || !read(sen::onto::people::attr::kSameAs).empty()
			|| !read(sen::onto::people::attr::kHomepage).empty();
	};

	const std::string folder = sensei::person::StatusFolder(file);
	if (!hasSource() && sensei::person::StatusFiles(folder).empty()) {
		if (sen::plugins::EnrichOnDemand()) {
			sensei::EnrichNow(kRobinSignature, file);
		} else {
			sensei::Notify("SEN Personal Relation Navigator", "Nothing to show of this person yet",
				(std::string(name.String()) + " has no feed, no links and no homepage that posts could come from. Enrich the person first (Robin looks for them); "
					"or turn on \"Enrich automatically on demand\" in the settings of SEN, under Plugins > Enrich.").c_str(), false, &file);
			printf("%s: no source of posts (the setting Enrich automatically on demand is off)\n", name.String());
		}
	}

	// the posts are old (or were never fetched): Robin fetches them again, and says nothing
	if (hasSource()) {
		const long age = sensei::person::StatusAge(folder);
		if (age < 0 || age > sensei::person::kRefreshSeconds) {
			if (fDebug)
				printf("%s: the posts are %s: asking Robin\n", name.String(), age < 0 ? "not there" : "old");
			sensei::EnrichNow(kRobinSignature, file, 40 * 1000000LL, "posts");
		}
	}

	for (const entry_ref& ref : sensei::person::StatusFiles(folder)) {
		Status status;
		status.ref = ref;
		status.title = ref.name;
		BNode statusNode(&ref);
		BString title;
		if (statusNode.InitCheck() == B_OK && statusNode.ReadAttrString("dc:title", &title) == B_OK && !title.IsEmpty())
			status.title = title.String();
		statuses->push_back(status);
	}
}


/** for a test from the command line: `SenPersonalRelationNavigator [--debug] <relation type> <person file>` prints what it finds */
void
App::ArgvReceived(int32 argc, char** argv)
{
	std::vector<std::string> arguments;
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--debug") == 0)
			fDebug = true;
		else
			arguments.push_back(argv[i]);
	}
	if (arguments.size() != 2) {
		std::cout << "Usage: SenPersonalRelationNavigator [--debug] <relation type> <person file>\n"
			<< "Prints what the relation shows for the person, e.g. relation/x-vnd.sen-labs.relation.person.status (the posts of the person).\n";
		Quit();
		return;
	}
	entry_ref file;
	if (get_ref_for_path(arguments[1].c_str(), &file) != B_OK) {
		std::cerr << arguments[1] << ": no such file" << std::endl;
		fExitCode = 1;
		Quit();
		return;
	}
	if (arguments[0] == sen::onto::people::mime::kStatusRelation) {
		std::vector<Status> statuses;
		_ResolveStatus(file, &statuses);
		for (const Status& status : statuses)
			printf("  %s  (%s)\n", status.title.c_str(), status.ref.name);
		if (statuses.empty())
			printf("  (nothing)\n");
	}
	Quit();
}


int
main()
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	App app;
	app.Run();
	return app.ExitCode();
}
