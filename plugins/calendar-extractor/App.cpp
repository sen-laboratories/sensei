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

#include "../../common/Notice.h"
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
		"  --no-description    enrich: no Markdown file for the description\n"
		"  --describe          print what would be written (nothing is changed)\n"
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
		bool found = true;
		const entry_ref target = sensei::RelationTarget(first, &found);
		if (!(target == first)) {
			if (found)
				be_roster->Launch(&target);
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
	uint32 what = message->what;
	if (what == B_REFS_RECEIVED)
		what = sensei::cmd::kEnrich;		// (the server asks for the contained relations of files of the types of this plugin: it has none)

	CalendarService service(fOptions);
	status_t result;
	switch (what) {
		case sensei::cmd::kIdentify:
			result = service.Identify(ref, &reply);
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
