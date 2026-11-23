/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <FindDirectory.h>
#include <Path.h>

#include <algorithm>
#include <climits>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sen/Sen.h>
#include <sen/SenAbout.h>
#include <sen/SenChoice.h>
#include <sen/SenPluginSettings.h>
#include <sen/Sensei.h>

#include "../../common/Notice.h"
#include "InstitutionFile.h"
#include "InstitutionRelations.h"
#include "core/InstitutionApi.h"

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.institution-enricher";
static const char* kUserAgent = "SEN-Campus/0.1 (Haiku; https://sen-labs.org)";
static const char* kNotifyGroup = "SEN Campus";

static const long kRecordCacheSeconds = 7 * 24 * 3600;		// (the registry changes a few times a year)
static const long kSearchCacheSeconds = 24 * 3600;
static const long kCountsCacheSeconds = 24 * 3600;			// (OpenAlex counts change every day, which is more than a file needs)
static const size_t kMaxChoices = 8;

namespace campus {

App::App()
	:
	BApplication(kApplicationSignature),
	fHttp(kUserAgent),
	fDebug(false),
	fExitCode(0)
{
}


void
App::AboutRequested()
{
	sen::ShowAbout();
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
	bool wipe = false, relations = true;
	int units = 0;
	// (what all plugins share is set in the settings of SEN: ask the user if several institutions fit, unless --auto says not to)
	bool automatic = !sen::plugins::LoadSharedSettings().askIfSeveral;
	std::vector<std::string> files;

	for (int32 i = 1; i < argc; i++) {
		const std::string arg = argv[i];
		if (arg == "--help" || arg == "-h") {
			std::cout << "Usage: SenInstitutionEnricher [options] <institution file...>\n\n"
			          << "SEN Campus: looks an institution up in the Research Organization Registry (ROR) and OpenAlex\n"
			          << "and writes its names, types, place, links and counts to the file.\n\n"
			          << "Options:\n"
			          << "  --auto, -a         Take the best match without asking\n"
			          << "  --wipe, -w         Replace what the file has\n"
			          << "  --units <n>        Also make the first n units of the institution (faculties, institutes) as institutions that are part of it (default 0)\n"
			          << "  --no-relations     Make no relations (part of) and no files for them\n"
			          << "  --debug, -d        Print debug logs to stdout\n"
			          << "  --help, -h         Show this help\n";
			Quit();
			return;
		} else if (arg == "--wipe" || arg == "-w") {
			wipe = true;
		} else if (arg == "--auto" || arg == "-a") {
			automatic = true;
		} else if (arg == "--no-relations") {
			relations = false;
		} else if (arg == "--units" && i + 1 < argc) {
			units = std::max(0, atoi(argv[++i]));
		} else if (arg == "--debug" || arg == "-d") {
			fDebug = true;
		} else if (!arg.empty() && arg[0] != '-') {
			files.push_back(arg);
		}
	}

	for (const std::string& file : files) {
		entry_ref ref;
		BEntry entry(file.c_str());
		if (entry.GetRef(&ref) != B_OK || !entry.Exists()) {
			std::cerr << file << ": no such file\n";
			fExitCode = 1;
			continue;
		}
		BString summary;
		if (EnrichOne(ref, wipe, automatic, relations, units, &summary) != B_OK)
			fExitCode = 1;
		else
			std::cout << file << ": " << summary.String() << "\n";
	}
	Quit();
}


status_t
App::Enrich(BMessage* message, BMessage* reply)
{
	(void) reply;
	const bool wipe = message->GetBool("wipe", false);
	const bool automatic = message->GetBool("auto", !sen::plugins::LoadSharedSettings().askIfSeveral);
	const bool relations = message->GetBool("relations", true);
	const int units = std::max(0, (int) message->GetInt32("units", 0));
	fDebug = message->GetBool("debug", fDebug);

	entry_ref ref;
	status_t lastResult = B_OK;
	for (int32 index = 0; message->FindRef("refs", index, &ref) == B_OK; index++) {
		BString summary;
		status_t result = EnrichOne(ref, wipe, automatic, relations, units, &summary);
		if (result != B_OK)
			lastResult = result;
	}
	return lastResult;
}


namespace {

/** `key = value` lines (`#` starts a comment) of a settings file: the value of the key, empty if there is none */
std::string
SettingOf(const char* fileName, const char* wanted)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK || path.Append("sen") != B_OK || path.Append(fileName) != B_OK)
		return std::string();
	std::ifstream in(path.Path());
	std::string line;
	while (std::getline(in, line)) {
		const size_t hash = line.find('#');
		if (hash != std::string::npos)
			line.erase(hash);
		const size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		auto trim = [](std::string text) {
			const size_t first = text.find_first_not_of(" \t\r");
			const size_t last = text.find_last_not_of(" \t\r");
			return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
		};
		if (trim(line.substr(0, equals)) == wanted)
			return trim(line.substr(equals + 1));
	}
	return std::string();
}


/** one answer as the dialog shows it: the name and where it is, what it is, how well it fits */
sen::Candidate
Describe(const institution::Hit& hit)
{
	const institution::Record& record = hit.record;
	sen::Candidate candidate;
	candidate.title = record.name.c_str();
	BString place;
	place << record.locality.c_str();
	if (!record.country.empty())
		place << (place.IsEmpty() ? "" : ", ") << record.country.c_str();
	candidate.subtitle = place;
	BString detail;
	for (size_t i = 0; i < record.types.size(); i++)
		detail << (i == 0 ? "" : ", ") << record.types[i].c_str();
	if (!record.otherNames.empty())
		detail << (detail.IsEmpty() ? "" : " - ") << record.otherNames[0].c_str();
	candidate.detail = detail;
	candidate.source = "ROR";
	candidate.score = hit.score;
	return candidate;
}

}	// namespace


// OpenAlex asks for an address (its polite pool) and has a free key: both are the ones of Paperboy, which also asks OpenAlex, unless this plugin has its own in `institution-enricher.settings`
void
App::LoadAccess()
{
	std::string email = SettingOf("institution-enricher.settings", "email");
	std::string key = SettingOf("institution-enricher.settings", "api_key");
	if (email.empty())
		email = SettingOf("paperboy.settings", "email");
	if (key.empty())
		key = SettingOf("paperboy.settings", "api_key");
	fAccess.mailto = email;
	fAccess.apiKey = key;
}


status_t
App::Find(const std::string& name, const std::string& rorId, bool automatic, institution::Record* record, BString* why)
{
	std::string body;
	int status = 0;

	if (!rorId.empty()) {
		if (!fHttp.Get(institution::RorRecordUrl(rorId), kRecordCacheSeconds, &body, &status)) {
			why->SetTo(status == 404 ? "The registry has no institution with the id " : "The registry does not answer (");
			*why << rorId.c_str() << (status == 404 ? "." : ").");
			return B_ENTRY_NOT_FOUND;
		}
		if (!institution::ParseRorRecord(body, record)) {
			why->SetTo("The answer of the registry could not be read.");
			return B_BAD_DATA;
		}
		return B_OK;
	}

	if (name.empty()) {
		why->SetTo("The file has no name to look for.");
		return B_BAD_VALUE;
	}
	if (!fHttp.Get(institution::RorAffiliationUrl(name), kSearchCacheSeconds, &body, &status)) {
		why->SetTo("The registry does not answer.");
		return B_ERROR;
	}
	std::vector<institution::Hit> hits = institution::ParseRorSearch(body);
	// (an institution that is gone is not offered if another one fits: a registry keeps the old records)
	std::stable_partition(hits.begin(), hits.end(), [](const institution::Hit& hit) { return hit.record.status != "withdrawn"; });
	if (hits.empty()) {
		why->SetTo("The registry knows no institution that fits \"");
		*why << name.c_str() << "\". An ROR id (the file's ROR attribute) or a link to the registry helps.";
		return B_ENTRY_NOT_FOUND;
	}

	// the registry says which one it takes; one that says so, and whose name is the name, is taken without asking
	const institution::Hit& best = hits[0];
	const bool clear = best.chosen && (hits.size() == 1 || institution::NameMatches(best.record, name) || hits[1].score < best.score);
	if (clear || automatic) {
		// (--auto takes the best, but only one that fits: the registry's choice, or the name)
		if (!best.chosen && !institution::NameMatches(best.record, name) && best.score < 0.9) {
			why->SetTo("No institution fits \"");
			*why << name.c_str() << "\" well enough to take it without asking.";
			return B_ENTRY_NOT_FOUND;
		}
		*record = best.record;
		return B_OK;
	}

	const size_t total = hits.size();
	if (hits.size() > kMaxChoices)
		hits.resize(kMaxChoices);
	std::vector<sen::Candidate> candidates;
	for (const institution::Hit& hit : hits)
		candidates.push_back(Describe(hit));
	BString prompt, explanation;
	prompt << "Found " << (int32) total << (total == 1 ? " result" : " results");
	explanation << (total == 1 ? "Is this the institution?" : "Which institution is it?");
	sen::ChoiceOptions options;
	options.multiple = false;
	options.explanation = explanation.String();
	const std::vector<int32> picked = sen::ChooseCandidates("Choose an institution", prompt.String(), candidates, options);
	if (picked.empty())
		return B_CANCELED;
	*record = hits[picked[0]].record;
	return B_OK;
}


bool
App::AddOpenAlex(institution::Record* record, BString* why)
{
	std::string body;
	int status = 0;
	if (!fHttp.Get(institution::OpenAlexInstitutionUrl(record->rorId, fAccess), kCountsCacheSeconds, &body, &status)) {
		if (status == 401 || status == 403 || status == 429) {
			why->SetTo(status == 429 ? "OpenAlex says that the daily budget of requests is used up" : "OpenAlex refused the request");
			*why << ": a free key (openalex.org/settings/api) as api_key in ~/config/settings/sen/institution-enricher.settings (or paperboy.settings) gives a budget of your own.";
		}
		return false;
	}
	return institution::MergeOpenAlex(body, record);
}


status_t
App::EnrichOne(const entry_ref& file, bool wipe, bool automatic, bool relations, int units, BString* summary)
{
	sensei::NotifyStarted(kNotifyGroup, "Enriching institution", file.name);

	FileState state;
	status_t result = ReadInstitution(file, &state);
	if (result != B_OK) {
		sensei::Notify(kNotifyGroup, "Institution not enriched", "Could not read the file.", false);
		return result;
	}

	fHttp.SetDebug(fDebug);
	LoadAccess();

	institution::Record record;
	BString why;
	result = Find(state.name, state.rorId, automatic, &record, &why);
	if (result == B_CANCELED)
		return B_CANCELED;
	if (result != B_OK) {
		sensei::Notify(kNotifyGroup, "Institution not enriched", why.String(), false);
		return result;
	}

	BString counts;
	const bool counted = AddOpenAlex(&record, &counts);

	// the name of the file is a guess when the file has no name of its own: the registry's name is the name now
	result = WriteInstitution(file, record, wipe);
	if (result != B_OK) {
		sensei::Notify(kNotifyGroup, "Institution not enriched", "Could not write the attributes to the file.", false);
		return result;
	}

	RelationResult made;
	if (relations)
		made = MakeRelations(file, record, units, fDebug);

	BString detail;
	detail << record.name.c_str();
	if (!record.locality.empty() || !record.country.empty())
		detail << " (" << record.locality.c_str() << (record.locality.empty() || record.country.empty() ? "" : ", ") << record.country.c_str() << ")";
	if (!record.types.empty()) {
		detail << "; ";
		for (size_t i = 0; i < record.types.size(); i++)
			detail << (i == 0 ? "" : ", ") << record.types[i].c_str();
	}
	if (counted && record.worksCount >= 0)
		detail << "; " << (int32) std::min<int64_t>(record.worksCount, INT32_MAX) << " works";
	if (made.related > 0)
		detail << "; " << made.related << " relations";
	if (!counted && !counts.IsEmpty())
		detail << ". " << counts;
	if (summary != nullptr)
		*summary = detail;

	sensei::Notify(kNotifyGroup, "Institution enriched", detail.String(), true, &file);
	return B_OK;
}

}	// namespace campus


int
main()
{
	campus::App app;
	app.Run();
	return app.ExitCode();
}
