/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <fs_attr.h>

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sen/Sen.h>
#include <sen/SenAbout.h>
#include <sen/Sensei.h>

#include "../../common/Notice.h"
#include "PersonWriter.h"
#include "StatusStore.h"
#include "core/PersonEnricher.h"

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.person-enricher";
static const char* kUserAgent = "SEN-Robin/0.1 (Haiku; https://sen-labs.org)";


namespace robin {

App::App()
	:
	BApplication(kApplicationSignature),
	fHttp(kUserAgent),
	fDebug(false),
	fExitCode(0)
{
}


App::~App()
{
}


void
App::AboutRequested()
{
	sen::ShowAbout();
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
	BApplication::MessageReceived(message);
}


void
App::Pulse()
{
	if (fSession.Expired())
		Quit();
}


void
App::RefsReceived(BMessage* message)
{
	BMessage reply(sensei::cmd::kResult);
	status_t result = Enrich(message, &reply);
	if (result != B_OK && result != B_CANCELED)
		fExitCode = 1;
	reply.AddInt32(sensei::key::kResult, result);
	reply.AddString(sen::key::kDetail, strerror(result));
	message->SendReply(&reply, this);
	Quit();
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	bool wipe = false;
	bool automatic = false;
	std::vector<std::string> files;

	for (int32 i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--help" || arg == "-h") {
			std::cout << "Usage: SenPersonEnricher [options] <person file...>\n\n"
			          << "Robin - Enriching People: Enriches person entities with public photos,\n"
			          << "profile information, verified links, and live microfeeds.\n\n"
			          << "Options:\n"
			          << "  --auto, -a     Enrich without interactive prompts\n"
			          << "  --wipe, -w     Replace existing photo and cached data\n"
			          << "  --debug, -d    Print debug logs to stdout\n"
			          << "  --help, -h     Show this help\n";
			Quit();
			return;
		} else if (arg == "--wipe" || arg == "-w") {
			wipe = true;
		} else if (arg == "--auto" || arg == "-a") {
			automatic = true;
		} else if (arg == "--debug" || arg == "-d") {
			fDebug = true;
		} else if (!arg.empty() && arg[0] != '-') {
			files.push_back(arg);
		}
	}

	for (const auto& file : files) {
		entry_ref ref;
		BEntry entry(file.c_str());
		if (entry.GetRef(&ref) != B_OK || !entry.Exists()) {
			std::cerr << file << ": no such file\n";
			fExitCode = 1;
			continue;
		}
		BString summary;
		if (EnrichOne(ref, wipe, automatic, &summary) != B_OK)
			fExitCode = 1;
	}
	Quit();
}


status_t
App::Enrich(BMessage* message, BMessage* reply)
{
	bool wipe = message->GetBool("wipe", false);
	bool automatic = message->GetBool("auto", true);
	fDebug = message->GetBool("debug", fDebug);

	// the personal relation navigator asks for the posts only (`posts`): the status relation is opened and they are old; nothing else of the person is looked for, and nothing is said
	const bool postsOnly = message->GetBool("posts", false);

	entry_ref ref;
	status_t lastResult = B_OK;

	for (int32 index = 0; message->FindRef("refs", index, &ref) == B_OK; index++) {
		BString summary;
		status_t res = postsOnly ? RefreshPostsOf(ref) : EnrichOne(ref, wipe, automatic, &summary);
		if (res != B_OK)
			lastResult = res;
	}
	return lastResult;
}


status_t
App::RefreshPostsOf(const entry_ref& file)
{
	PersonProfile profile;
	status_t result = ReadPerson(file, &profile);
	if (result != B_OK)
		return result;
	fHttp.SetDebug(fDebug);
	fSettings = Settings();
	LoadSettings(&fSettings);
	PersonEnricher enricher(fHttp.Function(), fSettings);
	// (force: the navigator knows that they are old)
	if (!RefreshStatus(file, &profile, &enricher, kFeedCacheSeconds, true))
		return B_ENTRY_NOT_FOUND;
	WriteStatusAttributes(file, profile);
	return B_OK;
}


status_t
App::EnrichOne(const entry_ref& file, bool wipe, bool automatic, BString* summary)
{
	sensei::NotifyStarted("SEN Robin", "Enriching person", file.name);

	PersonProfile profile;
	status_t result = ReadPerson(file, &profile);
	if (result != B_OK) {
		sensei::Notify("SEN Robin", "Person not enriched", "Could not read person file.", false);
		return result;
	}

	fHttp.SetDebug(fDebug);
	fSettings = Settings();
	LoadSettings(&fSettings);
	PersonEnricher enricher(fHttp.Function(), fSettings);
	bool enriched = enricher.Enrich(&profile, wipe);

	if (!enriched) {
		sensei::Notify("SEN Robin", "Person not enriched", "No public profile or avatar found.", false);
		return B_ENTRY_NOT_FOUND;
	}

	result = WritePerson(file, profile, wipe, fSettings);
	if (result != B_OK) {
		sensei::Notify("SEN Robin", "Person not enriched", "Could not write attributes to person file.", false);
		return result;
	}

	BString detail;
	if (profile.HasAvatar())
		detail << "Photo (" << profile.avatarCredit.c_str() << "); ";
	if (!profile.jobTitle.empty())
		detail << profile.jobTitle.c_str() << "; ";
	if (!profile.latestStatus.empty())
		detail << "Status: " << profile.latestStatus.c_str();

	if (summary != nullptr)
		*summary = detail;

	sensei::Notify("SEN Robin", "Person enriched", detail.String(), true, &file);
	return B_OK;
}


} // namespace robin


int
main()
{
	robin::App app;
	app.Run();
	return app.ExitCode();
}
