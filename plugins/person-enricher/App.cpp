/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Alert.h>
#include <Bitmap.h>
#include <DataIO.h>
#include <TranslationUtils.h>
#include <fs_attr.h>

#include <algorithm>
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
#include "PersonWriter.h"
#include "StatusStore.h"
#include "core/Hunt.h"
#include "core/NameMatch.h"
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
	// (what all plugins share is set in the settings of SEN: ask the user if several people fit, unless --auto says not to)
	bool automatic = !sen::plugins::LoadSharedSettings().askIfSeveral;
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
	bool automatic = message->GetBool("auto", !sen::plugins::LoadSharedSettings().askIfSeveral);
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


namespace {

const int32 kMaxChoices = 8;

BBitmap*
BitmapFromData(const std::string& data)
{
	if (data.empty())
		return NULL;
	BMemoryIO stream(data.data(), data.size());
	return BTranslationUtils::GetBitmap(&stream);
}

// the shorter side of a picture, in pixels (0 if no translator reads it): the larger one is the better photo
float
PictureSide(const std::string& data)
{
	BBitmap* bitmap = BitmapFromData(data);
	if (bitmap == NULL)
		return 0;
	const float side = std::min(bitmap->Bounds().Width(), bitmap->Bounds().Height()) + 1;
	delete bitmap;
	return side;
}

BString
Shorter(const std::string& text, size_t length)
{
	BString result(text.c_str());
	// (one line: line breaks of a bio are blanks here)
	result.ReplaceAll("\n", " ");
	if ((size_t)result.CountChars() > length) {
		result.TruncateChars((int32)length - 1);
		result << B_UTF8_ELLIPSIS;
	}
	return result;
}

/** One answer as it is shown in the dialog: the name, where it is and who it says it is, what else it says, where it comes from, the score; the photo comes later. */
sen::Candidate
DescribeHit(const HuntHit& hit)
{
	sen::Candidate candidate;
	candidate.title = hit.name.c_str();
	candidate.subtitle << HuntServiceName(hit.service) << " - " << (hit.service == "bluesky" || hit.service == "mastodon" ? "@" : "") << hit.id.c_str();
	BString detail;
	if (!hit.jobTitle.empty())
		detail << hit.jobTitle.c_str() << " - ";
	if (!hit.company.empty())
		detail << hit.company.c_str() << " - ";
	if (!hit.location.empty())
		detail << hit.location.c_str() << " - ";
	detail << Shorter(hit.description, 120);
	candidate.detail = detail;
	// (the name of the service without what is in brackets: "Mastodon (Fediverse)" is "Mastodon" above the bar, the subtitle has the rest)
	candidate.source = HuntServiceName(hit.service);
	const int32 bracket = candidate.source.FindFirst(" (");
	if (bracket > 0)
		candidate.source.Truncate(bracket);
	candidate.score = hit.score;
	candidate.imageUrl = hit.avatarUrl.c_str();
	return candidate;
}

// What the hit says about the person goes into the profile where it has nothing yet: what the file stored before wins, and so does the hit that is applied first. Several hits are merged
// from the top of the list to the bottom (later the user may reorder them to choose what wins); their pages go into the links (the next enrichment finds the accounts by them).
void
ApplyHit(PersonProfile* profile, const HuntHit& hit)
{
	auto addLink = [profile](const std::string& link) {
		if (!link.empty() && std::find(profile->sameAs.begin(), profile->sameAs.end(), link) == profile->sameAs.end())
			profile->sameAs.push_back(link);
	};
	addLink(hit.url);
	for (const std::string& link : hit.links)
		addLink(link);
	if (hit.service == "wikidata" && !hit.orcid.empty())
		addLink("https://orcid.org/" + hit.orcid);
	if (profile->homepage.empty())
		profile->homepage = hit.homepage;
	if (profile->bio.empty())
		profile->bio = hit.description;
	if (profile->location.empty())
		profile->location = hit.location;
	if (profile->company.empty())
		profile->company = hit.company;
	if (profile->jobTitle.empty())
		profile->jobTitle = hit.jobTitle;
	if (profile->orcid.empty())
		profile->orcid = hit.orcid;
	// the handle that the person is known by on the service becomes the handle of the file, if it has none
	if (profile->nick.empty()) {
		if (hit.service == "bluesky" || hit.service == "github")
			profile->nick = hit.id;
		else if (hit.service == "mastodon")
			profile->nick = "@" + hit.id;
	}
}


/** the words of a name */
std::vector<std::string>
Words(const std::string& name)
{
	std::vector<std::string> words;
	std::string word;
	for (char c : name) {
		if (c == ' ') {
			if (!word.empty())
				words.push_back(word);
			word.clear();
		} else {
			word += c;
		}
	}
	if (!word.empty())
		words.push_back(word);
	return words;
}


/**
 * The account of the person calls it by a variant of the name that the file has ("Scot Hacker" for "Scott Hacker": the file may have the typo): what the file has is never replaced on its own,
 * the user is asked (not with --auto: it is said in the summary instead). A variant is meant, never another name (a nickname, a pseudonym). Enrich (Overwrite) is the user's word to take it. If the
 * name of the file is what is wrong, it may be renamed, too (@p renameFile). True if the profile has the name of the account now.
 */
bool
CorrectName(PersonProfile* profile, const entry_ref& file, bool automatic, bool overwrite, bool* renameFile, std::string* note)
{
	*renameFile = false;
	if (profile->accountName.empty() || CompareNames(profile->name, profile->accountName) != NameMatch::Variant)
		return false;
	const std::string stored = profile->name, account = profile->accountName;
	if (automatic) {
		*note = "The account calls the person \"" + account + "\", the file says \"" + stored + "\"";
		return false;
	}
	bool take = overwrite;
	if (!overwrite) {
		const bool nameOfFile = stored == file.name;
		BString text;
		text << "The account calls this person \"" << account.c_str() << "\". The file says \"" << stored.c_str() << "\". Is that a typo in the file?";
		BString use;
		use << "Use \"" << account.c_str() << "\"";
		BAlert* alert = new BAlert("name", text.String(), "Keep", use.String(), nameOfFile ? "Use and rename the file" : NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		const int32 answer = alert->Go();
		take = answer >= 1;
		*renameFile = answer == 2;
	}
	if (!take)
		return false;

	// (the parts of the name that were the words of the old one are the words of the new one)
	const std::vector<std::string> before = Words(stored), after = Words(account);
	if (!before.empty() && !after.empty()) {
		if (profile->givenName == before.front())
			profile->givenName = after.front();
		if (profile->familyName == before.back())
			profile->familyName = after.back();
	}
	profile->previousName = stored;
	profile->name = account;
	profile->replaceName = true;
	return true;
}

}	// namespace


// The people that fit the name and the handle of the file, on the services that are on: one that is clearly the one is taken, else the user chooses (with their photos, which come after the
// window) unless --auto says to take the best. Returns the hit; false if there is none or the user cancelled (`cancelled` says which).
bool
App::_Hunt(const PersonProfile& profile, bool automatic, std::vector<HuntHit>* chosen, bool* cancelled)
{
	*cancelled = false;
	Hunter hunter(fHttp.Function(), fSettings);
	std::vector<HuntHit> hits = hunter.Search(profile);
	if (hits.empty())
		return false;

	if (Hunter::IsClearWinner(hits) || automatic) {
		chosen->push_back(hits[0]);
		return true;
	}

	const size_t total = hits.size();
	if ((int32)hits.size() > kMaxChoices)
		hits.resize(kMaxChoices);
	std::vector<sen::Candidate> candidates;
	for (const HuntHit& hit : hits)
		candidates.push_back(DescribeHit(hit));

	// the heading says what was found, the line under it what to do with it
	BString prompt, explanation;
	if (total == 1) {
		prompt << "Found 1 result";
		explanation << "Is this the person?";
	} else if (total > hits.size()) {
		prompt << "Found " << (int32)total << " results, showing the best " << (int32)hits.size();
		explanation << "Select matches you want to keep to merge results. Add a handle or a link to narrow it down.";
	} else {
		prompt << "Found " << (int32)total << " results";
		explanation << "Select matches you want to keep to merge results.";
	}

	// (the photos are fetched after the window is shown, the best match first; the user can choose before they are there)
	const sen::ImageLoader loader = [this](const BString& url) -> BBitmap* {
		std::string data;
		int status = 0;
		if (!fHttp.Get(url.String(), "image/*, */*;q=0.5", kProfileCacheSeconds, &data, &status))
			return NULL;
		return BitmapFromData(data);
	};
	// (one or several: a person that is on GitHub and has an ORCID is two hits that are merged into one profile)
	sen::ChoiceOptions options;
	options.multiple = total > 1;
	options.reorderable = true;		// (the first hit wins where they differ: the user can put the one they trust most on top)
	options.loader = loader;
	options.explanation = explanation.String();
	const std::vector<int32> picked = sen::ChooseCandidates("Choose a person", prompt.String(), candidates, options);
	if (picked.empty()) {
		*cancelled = true;
		return false;
	}
	for (int32 index : picked)
		chosen->push_back(hits[index]);
	return true;
}


// the best photo of those that were found: the larger one (a photo that is only 80 pixels is a thumbnail that was made for a list); the one that the enrichment has stays if it is as large
void
App::_BestPhoto(PersonProfile* profile, const HuntHit& hit)
{
	if (!fSettings.photo || hit.avatarUrl.empty())
		return;
	std::string data;
	int status = 0;
	if (!fHttp.Get(hit.avatarUrl, "image/*, */*;q=0.5", kProfileCacheSeconds, &data, &status))
		return;
	const float theirs = PictureSide(data);
	if (theirs < 64)
		return;
	if (!profile->HasAvatar() || theirs > PictureSide(profile->avatarData)) {
		profile->avatarData = data;
		profile->avatarUrl = hit.avatarUrl;
		profile->avatarCredit = HuntServiceName(hit.service);
		profile->avatarLicense.clear();
	}
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
	const bool hadSources = !PersonEnricher::Sources(profile, fSettings).empty();
	bool enriched = enricher.Enrich(&profile, wipe);

	// A person with a name or a handle but nothing that says which account is its own (or that the sources gave nothing for): the services are searched for it, and what is found is
	// what the file says now: its links, and the next enrichment follows them.
	std::vector<HuntHit> found;
	bool hunted = false;
	if (!hadSources || !enriched) {
		bool cancelled = false;
		if (_Hunt(profile, automatic, &found, &cancelled)) {
			hunted = true;
			// (merged in the order of the list: what a hit says is not replaced by the ones below it, nor is anything the file had; the links of all of them are kept)
			for (const HuntHit& hit : found) {
				ApplyHit(&profile, hit);
				if (profile.accountName.empty() && !hit.name.empty())
					profile.accountName = hit.name;
			}
			enricher.Enrich(&profile, false);		// (the links that the hits brought: their accounts, photos, posts)
			enriched = true;
			for (const HuntHit& hit : found)
				_BestPhoto(&profile, hit);
		} else if (cancelled) {
			return B_CANCELED;
		}
	}

	if (!enriched || (!hunted && !hadSources)) {
		BString text(PersonEnricher::Explain(profile, fSettings).c_str());
		if (!hunted && !fSettings.HuntServices().empty() && (!profile.name.empty() || !profile.nick.empty())) {
			text.SetTo("Searched ");
			const std::vector<std::string> services = fSettings.HuntServices();
			for (size_t i = 0; i < services.size(); i++)
				text << (i == 0 ? "" : (i + 1 == services.size() ? " and " : ", ")) << HuntServiceName(services[i]);
			text << " for " << (profile.name.empty() ? profile.nick.c_str() : profile.name.c_str()) << ": nobody fits. A handle or a link of the person helps.";
		}
		sensei::Notify("SEN Robin", "Person not enriched", text.String(), false);
		return B_ENTRY_NOT_FOUND;
	}

	// a typo in the name that the account shows: the user is asked
	bool renameFile = false;
	std::string nameNote;
	const bool nameCorrected = CorrectName(&profile, file, automatic, wipe, &renameFile, &nameNote);

	result = WritePerson(file, profile, wipe, fSettings);
	if (result != B_OK) {
		sensei::Notify("SEN Robin", "Person not enriched", "Could not write attributes to person file.", false);
		return result;
	}

	entry_ref current = file;
	if (renameFile) {
		BEntry entry(&file);
		if (entry.Rename(profile.name.c_str()) == B_OK)
			entry.GetRef(&current);
		else
			nameNote = "The file could not be renamed (there is one of that name already)";
	}

	BString detail;
	if (hunted) {
		detail << "Found ";
		for (size_t i = 0; i < found.size(); i++)
			detail << (i == 0 ? "" : " + ") << found[i].name.c_str() << " (" << HuntServiceName(found[i].service) << ")";
		detail << "; ";
	}
	if (nameCorrected)
		detail << "Name: " << profile.name.c_str() << (renameFile && current != file ? " (file renamed)" : "") << "; ";
	else if (!nameNote.empty())
		detail << nameNote.c_str() << "; ";
	if (profile.HasAvatar())
		detail << "Photo (" << profile.avatarCredit.c_str() << "); ";
	if (!profile.jobTitle.empty())
		detail << profile.jobTitle.c_str() << "; ";
	if (!profile.latestStatus.empty())
		detail << "Status: " << profile.latestStatus.c_str();

	if (summary != nullptr)
		*summary = detail;

	sensei::Notify("SEN Robin", "Person enriched", detail.String(), true, &current);
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
