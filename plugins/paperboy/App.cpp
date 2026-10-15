/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <Alert.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <sen/Sen.h>
#include <sen/Sensei.h>
#include <spdlog/spdlog.h>

#include "PaperSettings.h"
#include "Paperboy.h"

const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.paperboy";

namespace {

/** the file with the settings, next to those of SEN */
const char* const kSettingsFile = "sen/paperboy.settings";
const char* const kEmailVariable = "SEN_PAPERBOY_EMAIL";
const char* const kLogVariable = "SEN_LOG_LEVEL";


void
ReadSettings(paperboy::Settings* settings)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK && path.Append(kSettingsFile) == B_OK) {
		BFile file(path.Path(), B_READ_ONLY);
		off_t size = 0;
		if (file.InitCheck() == B_OK && file.GetSize(&size) == B_OK && size > 0 && size < 64 * 1024) {
			std::string text(size, '\0');
			if (file.Read(&text[0], size) == size)
				paperboy::ParseSettings(text, settings);
		}
	}

	const char* email = getenv(kEmailVariable);
	if (email != NULL && paperboy::LooksLikeEmail(email))
		settings->email = email;
}

}	// namespace


App::App()
	:
	BApplication(kApplicationSignature),
	fPaperboy(NULL),
	fDebug(false),
	fExitCode(0)
{
	if (const char* level = getenv(kLogVariable))
		spdlog::set_level(spdlog::level::from_str(level));

	ReadSettings(&fSettings);
}


App::~App()
{
	delete fPaperboy;
}


Paperboy*
App::_Service()
{
	// made when the settings are final: the command line may change them
	if (fPaperboy == NULL)
		fPaperboy = new Paperboy(fSettings);
	return fPaperboy;
}


void
App::_PrintUsage(const char* error) const
{
	if (error != NULL)
		std::cerr << "error: " << error << std::endl;
	std::cout <<
		"Usage: SenPaperboy [options] <file>...\n"
		"Finds the metadata of scientific papers (PDF, EPUB) by their DOI and the places where they can be read for free.\n"
		"\n"
		"  --identify        only find out whether the file is a paper and set its semantic type (no network)\n"
		"  --enrich          look the paper up and write its metadata, open access links and citations into the attributes\n"
		"  --extract         print the papers that it cites (the default)\n"
		"  --self            ask for the contained relations (a paper has none)\n"
		"  --local           extract: only the cited papers that are on this computer\n"
		"  --rename          enrich: rename the file after the pattern of the settings\n"
		"  --overwrite       enrich: replace title, authors and so on that the file already has\n"
		"  --email <address> address for the polite pool of OpenAlex and Unpaywall (or " << kEmailVariable << ")\n"
		"  --no-unpaywall    do not ask Unpaywall, use the open access data of OpenAlex\n"
		"  --debug           print the reply\n"
		"\n"
		"Settings: ~/config/settings/" << kSettingsFile << " (key = value lines: email, unpaywall, rename, rename_pattern,\n"
		"overwrite, title_search, max_pages, local_only).\n";
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	uint32 what = sensei::cmd::kExtract;
	bool self = false, local = false, rename = false, overwrite = false;
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
		} else if (strcmp(arg, "--extract") == 0) {
			what = sensei::cmd::kExtract;
		} else if (strcmp(arg, sensei::kOptionSelf) == 0) {
			self = true;
		} else if (strcmp(arg, "--local") == 0) {
			local = true;
		} else if (strcmp(arg, "--rename") == 0) {
			rename = true;
		} else if (strcmp(arg, "--overwrite") == 0) {
			overwrite = true;
		} else if (strcmp(arg, "--no-unpaywall") == 0) {
			fSettings.unpaywall = false;
		} else if (strcmp(arg, "--debug") == 0) {
			fDebug = true;
			spdlog::set_level(spdlog::level::debug);
		} else if (strcmp(arg, "--email") == 0 && i + 1 < argc) {
			if (!paperboy::LooksLikeEmail(argv[++i])) {
				_PrintUsage("that is not an e-mail address");
				Quit();
				return;
			}
			fSettings.email = argv[i];
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
		Quit();
		return;
	}

	// the options of the command line are those of the settings, too
	if (rename)
		fSettings.rename = true;
	if (overwrite)
		fSettings.overwrite = true;
	if (local)
		fSettings.localOnly = true;

	for (const std::string& file : files) {
		BEntry entry(file.c_str(), true);
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK || !entry.Exists()) {
			std::cerr << file << ": no such file" << std::endl;
			fExitCode = 1;
			continue;
		}
		BMessage message(what);
		message.AddRef("refs", &ref);
		message.AddBool(sen::conf::kSelf, self);
		_Handle(&message);
	}
	Quit();
}


void
App::RefsReceived(BMessage* message)
{
	// B_REFS_RECEIVED is how the server asks for the extraction
	_Handle(message);
	Quit();
}


void
App::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case sensei::cmd::kIdentify:
		case sensei::cmd::kEnrich:
		case sensei::cmd::kExtract:
			_Handle(message);
			Quit();
			break;
		default:
			BApplication::MessageReceived(message);
	}
}


status_t
App::_Handle(BMessage* message)
{
	entry_ref ref;
	if (message->FindRef("refs", &ref) != B_OK) {
		spdlog::error("no file to work with");
		BMessage reply(sensei::cmd::kResult);
		reply.AddInt32(sensei::key::kResult, B_BAD_VALUE);
		reply.AddString(sen::key::kDetail, strerror(B_BAD_VALUE));
		message->SendReply(&reply, this);
		return B_BAD_VALUE;
	}

	BMessage reply(sensei::cmd::kResult);
	status_t result;
	switch (message->what) {
		case sensei::cmd::kIdentify:
			result = _Service()->Identify(ref, &reply);
			break;
		case sensei::cmd::kEnrich:
			result = _Service()->Enrich(&ref, &reply);
			break;
		default:
			result = _Service()->Extract(ref, message->GetBool(sen::conf::kSelf, false), &reply);
			break;
	}

	// the result of the plugin is an int32 status_t (the server reads it), with a text for people and logs
	reply.AddInt32(sensei::key::kResult, result);
	reply.AddString(sen::key::kDetail, result == B_ENTRY_NOT_FOUND ? "no paper found" : strerror(result));

	if (result != B_OK)
		fExitCode = 1;
	if (fDebug)
		reply.PrintToStream();

	// the command line has nobody to answer, the server does; a reply that nobody takes is ignored
	message->SendReply(&reply, this);
	return result;
}


int
main()
{
	App app;
	if (app.InitCheck() != B_OK)
		return 1;
	app.Run();
	return app.ExitCode();
}
