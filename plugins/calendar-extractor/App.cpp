/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Entry.h>
#include <Roster.h>

#include <stdio.h>
#include <string.h>

#include <iostream>
#include <string>
#include <vector>

#include <sen/Sen.h>
#include <sen/SenAbout.h>
#include <sen/SenMessages.h>
#include <sen/SenOntoCore.h>
#include <sen/SenOntoPeople.h>
#include <sen/SenOntoSpatial.h>
#include <sen/SenOntoTemporal.h>
#include <sen/SenAttributes.h>
#include <sen/Sensei.h>

#include "core/IcsText.h"
#include "../../common/Notice.h"
#include "../../common/OpenTarget.h"
#include "../../common/RelationTarget.h"

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.calendar-extractor";


App::App()
	:
	BApplication(kApplicationSignature),
	fExitCode(0),
	fNotify(true)
{
	// the options are the settings, the command line may change them
	calendar::Settings settings;
	calendar::LoadSettings(&settings);
	fOptions.rename = settings.rename;
	fOptions.namePattern = settings.renamePattern;
	fOptions.description = settings.description;
	fOptions.myself = settings.myself;
}


void
App::AboutRequested()
{
	sen::ShowAbout();
}


void
App::_PrintUsage(const char* error) const
{
	if (error != NULL)
		std::cerr << "error: " << error << std::endl;
	std::cout <<
		"Usage: SenCalendarExtractor [options] <file>...\n"
		"Reads calendar files (iCalendar .ics, vCalendar 1.0 .vcs): an event gets its type and its attributes.\n"
		"\n"
		"  --identify          only find out whether the file is an event and set its semantic type\n"
		"  --enrich            write the attributes, name the file after the event, keep its description in a Markdown file (the default)\n"
		"  --no-rename         enrich: keep the name of the file\n"
		"  --name-pattern <p>  enrich: name the file after this pattern ({date} {time} {year} {month} {day} {hour} {minute} {title}, default {date}_{time}_{title})\n"
		"  --no-description    enrich: no Markdown file for the description\n"
		"  --describe          print what would be written (nothing is changed)\n"
		"  --extract           print the events that a calendar file with several events contains (a self relation, what the server asks for)\n"
		"  --materialise <key> make the event with this identifier (the key of an item of --extract) a file of its own, in the folder of --library (repeat for several)\n"
		"  --resolve <type>    print the targets of a relation (relation/x-vnd.sen-labs.relation.event.attendee, .location, .series)\n"
		"  --library <folder>  resolve: make the people and places in that folder (else in the Library of the current context of SEN: a test leaves files there)\n";
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	fNotify = false;		// a person at the command line reads the output
	uint32 what = sensei::cmd::kEnrich;
	bool describe = false;
	std::string relationType;
	std::vector<std::string> keys;
	std::vector<std::string> files;
	for (int32 i = 1; i < argc; i++) {
		const char* arg = argv[i];
		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
			_PrintUsage();
			Quit();
			return;
		} else if (strcmp(arg, "--identify") == 0) {
			what = sensei::cmd::kIdentify;
		} else if (strcmp(arg, "--enrich") == 0) {
			what = sensei::cmd::kEnrich;
		} else if (strcmp(arg, "--describe") == 0) {
			describe = true;
		} else if (strcmp(arg, "--no-rename") == 0) {
			fOptions.rename = false;
		} else if (strcmp(arg, "--name-pattern") == 0 && i + 1 < argc) {
			std::string why;
			if (!ics::IsValidNamePattern(argv[++i], &why)) {
				_PrintUsage(why.c_str());
				fExitCode = 1;
				Quit();
				return;
			}
			fOptions.namePattern = argv[i];
		} else if (strcmp(arg, "--no-description") == 0) {
			fOptions.description = false;
		} else if (strcmp(arg, "--library") == 0 && i + 1 < argc) {
			BEntry folder(argv[++i], true);
			fHasLibrary = folder.GetRef(&fLibrary) == B_OK && folder.IsDirectory();
			if (!fHasLibrary) {
				_PrintUsage("that is no folder");
				fExitCode = 1;
				Quit();
				return;
			}
		} else if (strcmp(arg, "--extract") == 0) {
			what = sensei::cmd::kExtract;
		} else if (strcmp(arg, "--materialise") == 0 && i + 1 < argc) {
			what = sensei::cmd::kMaterialise;
			keys.push_back(argv[++i]);
		} else if (strcmp(arg, "--resolve") == 0 && i + 1 < argc) {
			what = sensei::cmd::kResolve;
			relationType = argv[++i];
		} else if (arg[0] == '-') {
			_PrintUsage((std::string("unknown option ") + arg).c_str());
			fExitCode = 1;
			Quit();
			return;
		} else {
			files.push_back(arg);
		}
	}
	if (files.empty()) {
		_PrintUsage("no file given");
		fExitCode = 1;
		Quit();
		return;
	}
	for (const std::string& name : files) {
		BEntry entry(name.c_str(), true);
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK || !entry.Exists()) {
			std::cerr << name << ": no such file" << std::endl;
			fExitCode = 1;
			continue;
		}
		if (describe) {
			CalendarService(fOptions).Describe(ref);
			continue;
		}
		BMessage message(what);
		message.AddRef("refs", &ref);
		if (what == sensei::cmd::kResolve)
			message.AddString(sen::key::kRelationType, relationType.c_str());
		if (what == sensei::cmd::kExtract)
			message.AddBool(sen::conf::kSelf, true);
		if (what == sensei::cmd::kMaterialise) {
			BMessage libraries;
			if (fHasLibrary)
				libraries.AddRef(sen::onto::temporal::mime::kEvent, &fLibrary);
			message.AddMessage(sensei::key::kLibraries, &libraries);
			for (const std::string& key : keys) {
				BMessage item;
				item.AddString(sen::onto::temporal::attr::kUid, key.c_str());
				item.AddString(sen::key::kItemId, key.c_str());
				message.AddMessage(sensei::key::kItems, &item);
			}
		}
		_Handle(&message);
	}
	Quit();
}


void
App::RefsReceived(BMessage* message)
{
	// the Enrich of the file manager (Akita) is B_REFS_RECEIVED; a double click on the file of a relation (this plugin is the application of the relation types) opens the target
	entry_ref first;
	if (message->FindRef("refs", &first) == B_OK) {
		// an item of the menu of the contained relations (an event of a calendar) is the target with the properties of the relation: it is opened, never enriched
		if (message->HasMessage(sen::key::kRelationProperties)) {
			sensei::OpenTarget(first);
			Quit();
			return;
		}
		bool found = true;
		const entry_ref target = sensei::RelationTarget(first, &found);
		if (!(target == first)) {
			if (found)
				sensei::OpenTarget(target);
			else
				fprintf(stderr, "%s: nothing to open (the target of the relation is gone).\n", first.name);
			Quit();
			return;
		}
	}
	_Handle(message);
	Quit();
}


void
App::Pulse()
{
	if (fSession.Expired())
		Quit();
}


void
App::MessageReceived(BMessage* message)
{
	// the settings window of SEN asks for the settings and sends the changes; the instance that is started for it ends with the session
	if (fSession.Handle(message, &fSettingsProvider, kApplicationSignature)) {
		if (fSession.IsEnded())
			Quit();
		else
			SetPulseRate(5000000);
		return;
	}
	switch (message->what) {
		case sensei::cmd::kIdentify:
		case sensei::cmd::kEnrich:
		case sensei::cmd::kResolve:
		case sensei::cmd::kMaterialise:
			_Handle(message);
			Quit();		// (a plugin is a multiple launch application that ends when it has answered)
			break;
		default:
			BApplication::MessageReceived(message);
	}
}


status_t
App::_Handle(BMessage* message)
{
	entry_ref ref;
	BMessage reply(sensei::cmd::kResult);
	if (message->FindRef("refs", &ref) != B_OK) {
		reply.AddInt32(sensei::key::kResult, B_BAD_VALUE);
		reply.AddString(sen::key::kDetail, strerror(B_BAD_VALUE));
		message->SendReply(&reply, this);
		return B_BAD_VALUE;
	}
	// What the server sends for an extraction is B_REFS_RECEIVED with SEN:self (true or false); the Enrich of the file manager is B_REFS_RECEIVED without it.
	uint32 what = message->what;
	if (what == B_REFS_RECEIVED)
		what = message->HasBool(sen::conf::kSelf) ? sensei::cmd::kExtract : sensei::cmd::kEnrich;

	CalendarService service(fOptions);
	status_t result;
	switch (what) {
		case sensei::cmd::kIdentify:
			result = service.Identify(ref, &reply);
			break;
		case sensei::cmd::kExtract:
		{
			// (the contained relations of a calendar with several events; the outgoing ones are not asked of an extractor)
			result = message->GetBool(sen::conf::kSelf, false) ? service.Extract(ref, &reply) : B_OK;
			BMessage root, children;
			if (!fNotify && reply.FindMessage(sensei::key::kItem, &root) == B_OK) {
				// (at the command line: the events that the server would get)
				const char* label;
				for (int32 i = 0; root.FindString(sensei::key::kLabel, i, &label) == B_OK; i++) {
					int64 start = 0;
					root.FindInt64(kStartAlias, i, &start);
					printf("  %lld  %s  %s\n", (long long)start, label, root.GetString(kUidAlias, i, ""));
				}
			}
			break;
		}
		case sensei::cmd::kMaterialise:
			result = service.Materialise(ref, *message, &reply);
			if (!fNotify) {
				// (at the command line: the events that the server would get)
				BMessage target;
				for (int32 i = 0; reply.FindMessage(sensei::key::kTargets, i, &target) == B_OK; i++) {
					entry_ref event;
					target.FindRef(sen::key::kTargetRef, &event);
					printf("  %s\n", event.name);
				}
			}
			break;
		case sensei::cmd::kResolve:
		{
			BMessage libraries;
			message->FindMessage(sensei::key::kLibraries, &libraries);
			if (fHasLibrary) {
				libraries.AddRef(sen::onto::people::mime::kPerson, &fLibrary);
				libraries.AddRef(sen::onto::spatial::mime::kPlace, &fLibrary);
			}
			result = service.Resolve(ref, message->GetString(sen::key::kRelationType, ""), libraries, &reply);
			if (!fNotify) {
				// (at the command line: what the server would get)
				BMessage item;
				for (int32 i = 0; reply.FindMessage(sensei::key::kTargets, i, &item) == B_OK; i++) {
					entry_ref target;
					item.FindRef(sen::key::kTargetRef, &target);
					printf("  %-14s %s", item.GetString(sen::attr::kRelationLabel, "?"), target.name);
					for (const char* name : {sen::onto::core::attr::kRelRole, sen::onto::temporal::attr::kResponse, sen::onto::temporal::attr::kRecurrenceId, sen::onto::temporal::attr::kTitle}) {
						if (item.HasString(name))
							printf("  %s=%s", name, item.GetString(name, ""));
					}
					int64 start;
					if (item.FindInt64(sen::onto::temporal::attr::kStartDate, &start) == B_OK)
						printf("  start=%lld", (long long)start);
					printf("\n");
				}
			}
			break;
		}
		default:
		{
			if (fNotify)
				sensei::NotifyStarted("SEN Calendar Extractor", "Enriching event", ref.name);
			result = service.Enrich(&ref, &reply);
			if (fNotify)
				sensei::Notify("SEN Calendar Extractor", service.SummaryTitle().c_str(), service.Summary().c_str(), result == B_OK, &ref);
			else if (!service.Summary().empty())
				std::cout << service.Summary() << std::endl;
			break;
		}
	}
	reply.AddInt32(sensei::key::kResult, result);
	reply.AddString(sen::key::kDetail, service.Summary().empty() ? strerror(result) : service.Summary().c_str());
	if (result != B_OK && result != B_ENTRY_NOT_FOUND)
		fExitCode = 1;
	// the command line has nobody to answer, the server does; a reply that nobody takes is ignored
	message->SendReply(&reply, this);
	return result;
}


int
main()
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	App app;
	if (app.InitCheck() != B_OK)
		return 1;
	app.Run();
	return app.ExitCode();
}
