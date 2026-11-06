/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Entry.h>
#include <FindDirectory.h>
#include <LocaleRoster.h>
#include <Node.h>
#include <Path.h>
#include <String.h>
#include <Url.h>
#include <fs_attr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <iostream>

#include <sen/Sen.h>
#include <sen/SenAbout.h>
#include <sen/SenChoice.h>
#include <sen/SenOntoSpatial.h>
#include <sen/SenPluginSettings.h>
#include <sen/Sensei.h>

#include "../../common/MappingUtil.h"
#include "../../common/Notice.h"
#include "../../common/ThumbnailUtil.h"
#include "PlaceWriter.h"

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.place-enricher";
// what the services are told that we are (Nominatim and Wikimedia want a program that says what it is and where to find it)
static const char* kUserAgent = "SEN-SUESS/0.1 (Haiku; https://sen-labs.org)";
// the answers are kept for a week; a thumbnail of 256 pixels is enough for the icon sizes of the file manager
static const long kCacheSeconds = 7 * 24 * 3600;
static const int kThumbnailWidth = 256;
// Nominatim: at most one request per second (a little more, for the jitter of the clocks)
static const long kNominatimGapMilliseconds = 1100;


App::App()
	:
	BApplication(kApplicationSignature),
	fMapper(new MappingUtil()),
	fHttp(NULL),
	fCache(NULL),
	fLimiter(NULL),
	fDebug(false),
	fLanguageGiven(false),
	fPositionGiven(false),
	fHasPositionHint(false),
	fOverwrite(true),
	fAuto(false),
	fExitCode(0)
{
	BPath folder;
	if (find_directory(B_USER_CACHE_DIRECTORY, &folder, true) == B_OK && folder.Append("sen/place-enricher") == B_OK) {
		fCache = new place::DiskCache(folder.Path());
		fLimiter = new place::RateLimiter(std::string(folder.Path()) + "/nominatim.last", kNominatimGapMilliseconds);
	}
	_ReloadSettings();
}


/** the language of the system as the code that the services take (`de`): the first of the preferred languages of the user */
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
	return std::string();
}


void
App::_ReloadSettings()
{
	const std::string language = fSettings.language;
	const bool position = fSettings.lookupByPosition;
	fSettings = place::Settings();
	place::LoadSettings(&fSettings);
	// (what all plugins share is set in the settings of SEN; `--auto` wins over it for one run)
	const sen::plugins::SharedSettings shared = sen::plugins::LoadSharedSettings();
	fSettings.choose = shared.askIfSeveral;
	if (fLanguageGiven)
		fSettings.language = language;
	if (fPositionGiven)
		fSettings.lookupByPosition = position;
	fLanguage = fSettings.language.empty() ? SystemLanguage() : fSettings.language;
}


App::~App()
{
	delete fHttp;
	delete fMapper;
	delete fCache;
	delete fLimiter;
}


int
main()
{
	setvbuf(stdout, NULL, _IOLBF, 0);		// (a log that ends with a crash is whole)
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


void
App::AboutRequested()
{
	sen::ShowAbout();
}


void
App::PrintUsage(const char* error)
{
	if (error != NULL)
		std::cerr << "error: " << error << std::endl;
	std::cout
		<< "Usage: SenPlaceEnricher [options] <file>...\n"
		<< "Looks a place up at OpenStreetMap Nominatim: writes its name, kind, position, bounding box, outline and address to the file, and a picture from Wikidata as its thumbnail.\n"
		<< "Data (c) OpenStreetMap contributors, Open Database License (https://www.openstreetmap.org/copyright).\n\n"
		<< "  -a, --auto         if several places are found, take the best without asking\n"
		<< "  -w, --wipe         replace attributes that the file has (the default for a request of the menu)\n"
		<< "  -l, --language <l> the language of the names (de, en, ...); the default is the language of the system\n"
		<< "  -p, --position     look a file up by its position (schema:latitude, schema:longitude) if it has no title: the position is sent to OpenStreetMap\n"
		<< "      --debug        print what is done\n\n"
		<< "Settings: the Settings window of SEN (Plugins, Enrich), kept in ~/config/settings/sen/suess.settings.\n";
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	// (started with no arguments: by the roster for a message, the settings window of SEN asks for the settings; nothing to do here)
	if (argc < 2)
		return;

	bool automatic = false, wipe = false;
	std::vector<std::string> files;
	for (int32 i = 1; i < argc; i++) {
		const char* arg = argv[i];
		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
			PrintUsage();
			Quit();
			return;
		} else if (strcmp(arg, "-a") == 0 || strcmp(arg, "--auto") == 0) {
			automatic = true;
		} else if (strcmp(arg, "-w") == 0 || strcmp(arg, "--wipe") == 0) {
			wipe = true;
		} else if (strcmp(arg, "-p") == 0 || strcmp(arg, "--position") == 0) {
			fSettings.lookupByPosition = true;
			fPositionGiven = true;
		} else if (strcmp(arg, "--debug") == 0) {
			fDebug = true;
		} else if ((strcmp(arg, "-l") == 0 || strcmp(arg, "--language") == 0) && i + 1 < argc) {
			fSettings.language = argv[++i];
			fLanguage = fSettings.language;
			fLanguageGiven = true;
		} else if (arg[0] == '-') {
			PrintUsage((std::string("unknown option ") + arg).c_str());
			fExitCode = 1;
			Quit();
			return;
		} else {
			files.push_back(arg);
		}
	}
	if (files.empty()) {
		PrintUsage("no file given");
		fExitCode = 1;
		Quit();
		return;
	}

	for (const std::string& file : files) {
		BEntry entry(file.c_str(), true);
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK || !entry.Exists()) {
			std::cerr << file << ": no such file" << std::endl;
			fExitCode = 1;
			continue;
		}
		BMessage message(B_REFS_RECEIVED);
		message.AddRef("refs", &ref);
		message.AddBool("wipe", wipe);
		message.AddBool("auto", automatic);
		message.AddBool("debug", fDebug);
		BMessage reply;
		if (Enrich(&message, &reply) != B_OK && reply.GetInt32(sensei::key::kResult, B_OK) != B_CANCELED)
			fExitCode = 1;
	}
	Quit();
}


void
App::RefsReceived(BMessage* message)
{
	_ReloadSettings();		// (this instance may have been started before the user changed the settings)
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


// (a notification, not a dialog: the plugin is not held up and nothing stays on the screen)
void
App::ShowError(const char* message)
{
	sensei::Notify("SEN SUESS", "Place not enriched", message, false);
	printf("%s\n", message);
}


status_t
App::Get(const std::string& url, Service service, std::string* body)
{
	if (fCache != NULL && fCache->Get(url, body, kCacheSeconds)) {
		if (fDebug)
			printf("kept answer for %s\n", url.c_str());
		return B_OK;
	}
	if (service == kNominatim && fLimiter != NULL) {
		const long waited = fLimiter->Wait();
		if (fDebug && waited > 0)
			printf("waited %ld ms for Nominatim\n", waited);
	}
	BUrl address(url.c_str(), false);
	status_t result = fHttp->FetchRemoteContent(address, body);
	if (result != B_OK) {
		const int status = fHttp->LastHttpStatus();
		if (status == 429)
			return B_BUSY;
		return status == 403 ? B_NOT_ALLOWED : B_ERROR;
	}
	if (fCache != NULL)
		fCache->Put(url, *body);
	return B_OK;
}


status_t
App::GetImage(const std::string& url, std::string* data)
{
	BUrl address(url.c_str(), false);
	return fHttp->FetchRemoteContent(address, data);
}


static std::string
KindOf(const place::Hit& hit)
{
	std::string kind = hit.addressType.empty() || hit.addressType == hit.category ? hit.type : hit.addressType;
	if (kind == "administrative" || kind.empty())
		kind = hit.addressType.empty() ? hit.category : hit.addressType;
	std::replace(kind.begin(), kind.end(), '_', ' ');
	return kind;
}


/** One answer as it is shown in the list of sen::ChooseCandidate(): the name, what it is and where, the whole line of Nominatim, the source. */
static sen::Candidate
Describe(const place::Hit& hit, float score)
{
	sen::Candidate candidate;
	candidate.title = hit.name.c_str();
	candidate.subtitle << KindOf(hit).c_str();
	BString where;
	if (!hit.locality.empty() && hit.locality != hit.name)
		where << hit.locality.c_str();
	if (!hit.country.empty()) {
		if (!where.IsEmpty())
			where << ", ";
		where << hit.country.c_str();
	}
	if (!where.IsEmpty()) {
		if (!candidate.subtitle.IsEmpty())
			candidate.subtitle << " - ";
		candidate.subtitle << where;
	}
	BString line(hit.displayName.c_str());
	if (line.CountChars() > 110) {
		line.TruncateChars(110);
		line << B_UTF8_ELLIPSIS;
	}
	candidate.detail = line;
	candidate.source = "OpenStreetMap";
	candidate.score = score;
	return candidate;
}


status_t
App::ChoosePlace(const std::vector<place::Hit>& hits, const std::string& wanted, place::Hit* chosen)
{
	std::vector<sen::Candidate> candidates;
	const size_t count = std::min<size_t>(hits.size(), 10);
	size_t best = count;
	float bestScore = -1;
	size_t matching = 0;
	for (size_t i = 0; i < count; i++) {
		const float score = place::ScoreHit(hits[i], (int)i, (int)hits.size(), wanted);
		candidates.push_back(Describe(hits[i], score));
		if (place::TitleMatches(hits[i], wanted))
			matching++;
		// (a place that is not the one that was asked for is no help, however well Nominatim ranks it: the best of those that fit)
		if ((!fAuto || place::TitleMatches(hits[i], wanted)) && score > bestScore) {
			best = i;
			bestScore = score;
		}
	}
	if (candidates.empty())
		return B_ENTRY_NOT_FOUND;

	if (fAuto) {
		if (best == count) {
			printf("got %zu places, none of them called '%s'.\n", hits.size(), wanted.c_str());
			return B_ENTRY_NOT_FOUND;
		}
		printf("got %zu places, taking the best: '%s'.\n", hits.size(), hits[best].name.c_str());
		*chosen = hits[best];
		return B_OK;
	}
	// one answer and it is the place that was asked for: nothing to ask
	if (hits.size() == 1 && matching == 1) {
		*chosen = hits[0];
		return B_OK;
	}

	printf("got %zu places, asking the user...\n", hits.size());
	const int32 picked = sen::ChooseCandidate("Choose a place", "OpenStreetMap found several places. Which one is yours?", candidates,
		sen::LoadPluginLogo(kApplicationSignature));
	if (picked < 0)
		return B_CANCELED;
	*chosen = hits[picked];
	return B_OK;
}


/** The name for a search: the title of the file, else its name (without the extension, an underscore is a blank). */
static std::string
NameOf(BNode& node, const entry_ref& file)
{
	BString text;
	if (node.ReadAttrString(sen::onto::spatial::attr::kTitle, &text) == B_OK && !text.IsEmpty())
		return text.String();
	std::string name = file.name;
	const size_t dot = name.rfind('.');
	if (dot != std::string::npos && dot > 0 && name.size() - dot <= 5)
		name.erase(dot);
	std::replace(name.begin(), name.end(), '_', ' ');
	return name;
}


static bool
ReadDouble(BNode& node, const char* attribute, double* value)
{
	return node.ReadAttr(attribute, B_DOUBLE_TYPE, 0, value, sizeof(*value)) == (ssize_t)sizeof(*value);
}


status_t
App::FindPlace(const entry_ref& file, place::Hit* hit)
{
	BNode node(&file);
	BString text;
	const std::string language = fLanguage;

	// a place that was enriched before has its link to OpenStreetMap: no search, no question
	std::string osmType;
	int64_t osmId = 0;
	if (node.ReadAttrString(sen::onto::spatial::attr::kSameAs, &text) == B_OK && place::OsmFrom(text.String(), &osmType, &osmId)) {
		std::string body;
		status_t result = Get(place::LookupUrl(osmType, osmId, language, fSettings.boundary), kNominatim, &body);
		if (result != B_OK)
			return result;
		std::vector<place::Hit> hits;
		if (place::ParseHits(body, &hits) && !hits.empty()) {
			*hit = hits[0];
			return B_OK;
		}
		// (the object is gone from OpenStreetMap: what the file says is searched for instead)
	}

	// no title but a position, and the user allows it: the place at that position
	double latitude = 0, longitude = 0;
	const bool hasTitle = node.ReadAttrString(sen::onto::spatial::attr::kTitle, &text) == B_OK && !text.IsEmpty();
	const bool hasPosition = ReadDouble(node, sen::onto::spatial::attr::kLatitude, &latitude) && ReadDouble(node, sen::onto::spatial::attr::kLongitude, &longitude);
	fHasPositionHint = hasPosition;
	if (!hasTitle && hasPosition && fSettings.lookupByPosition) {
		fWanted = "";
		std::string body;
		status_t result = Get(place::ReverseUrl(latitude, longitude, language, fSettings.boundary), kNominatim, &body);
		if (result != B_OK)
			return result;
		return place::ParseHit(body, hit) ? B_OK : B_ENTRY_NOT_FOUND;
	}

	const std::string name = NameOf(node, file);
	fWanted = name;
	if (name.empty())
		return B_ENTRY_NOT_FOUND;
	std::string body;
	status_t result = Get(place::SearchUrl(name, language, fSettings.boundary), kNominatim, &body);
	if (result != B_OK)
		return result;
	std::vector<place::Hit> hits;
	if (!place::ParseHits(body, &hits))
		return B_ERROR;
	if (hits.empty())
		return B_ENTRY_NOT_FOUND;
	return ChoosePlace(hits, name, hit);
}


/**
 * The work of the plugin: find the place, write what Nominatim says of it to the attributes of the file, and a picture of it as its thumbnail. The result is that of the PLACE: that the
 * description or the picture could not be had does not undo it.
 */
status_t
App::Enrich(BMessage* message, BMessage* reply)
{
	entry_ref ref;
	if (message->FindRef("refs", &ref) != B_OK) {
		ShowError("Failed to resolve input file.");
		return B_BAD_VALUE;
	}
	fDebug = message->GetBool("debug", fDebug);
	// the action decides what is replaced ("wipe": Enrich (Overwrite), the Shift key of the menu); the command line wins over the settings for one run
	fOverwrite = message->GetBool("wipe", true);
	fAuto = message->GetBool("auto", !fSettings.choose);

	if (!fAuto)		// (the lookup takes a while: say that it has begun; a script that asked for --auto is not told, as at the end)
		sensei::NotifyStarted("SEN SUESS", "Enriching place", ref.name);

	delete fHttp;
	fHttp = new BaseEnricher(&ref, fMapper);
	fHttp->SetUserAgent(kUserAgent);

	fHasPositionHint = false;
	place::Hit hit;
	status_t result = FindPlace(ref, &hit);
	if (result == B_CANCELED)
		return result;
	if (result == B_BUSY) {
		ShowError("OpenStreetMap says there are too many requests (the public service allows one per second). Try again in a moment.");
		return result;
	}
	if (result == B_NOT_ALLOWED) {
		ShowError("OpenStreetMap refused the request (403 Forbidden): the service did not accept this program or this computer. Try again later.");
		return result;
	}
	if (result == B_ENTRY_NOT_FOUND) {
		BString text("OpenStreetMap has no place called \"");
		text << fWanted.c_str() << "\". Set the title of the file (dc:title), or name the file after the place.";
		if (fHasPositionHint && !fSettings.lookupByPosition)
			text << " The file has a position: turn on \"Look up files by their position\" in the settings of SEN to look it up by that.";
		ShowError(text.String());
		return result;
	}
	if (result != B_OK) {
		ShowError("Failed to look up the place.");
		return result;
	}

	// an outline that is too large to keep (a country): asked for again, with a coarser tolerance (one more request, for those only)
	if (fSettings.boundary && hit.wkt.size() > place::kMaxBoundaryBytes && hit.hasBox && !hit.osmType.empty()) {
		std::string body;
		std::vector<place::Hit> coarse;
		if (Get(place::LookupUrl(hit.osmType, hit.osmId, fLanguage, true, place::BoundaryTolerance(hit)), kNominatim, &body) == B_OK && place::ParseHits(body, &coarse)
				&& !coarse.empty() && coarse[0].wkt.size() < hit.wkt.size()) {
			if (fDebug)
				printf("the outline of %s is %zu bytes, asked again for a coarser one: %zu bytes\n", hit.name.c_str(), hit.wkt.size(), coarse[0].wkt.size());
			hit.wkt = coarse[0].wkt;
		}
	}

	// what Wikidata says: the short description of the place in the language of the user, and the picture that it names (a failure of Wikidata does not undo the place)
	place::Entity entity;
	bool haveEntity = false;
	if (!hit.wikidataId.empty() && fSettings.picture) {
		std::string body;
		haveEntity = Get(place::EntityUrl(hit.wikidataId, fLanguage), kWikimedia, &body) == B_OK && place::ParseEntity(body, hit.wikidataId, fLanguage, &entity);
		if (!haveEntity)
			printf("could not get %s from Wikidata, skipping the description and the picture.\n", hit.wikidataId.c_str());
	}

	result = place::WritePlace(ref, hit, entity.description, fSettings.boundary, fOverwrite);
	if (result != B_OK) {
		ShowError("Failed to write the metadata to the file.");
		return result;
	}

	// the picture, like the cover of a book
	std::string pictured;
	if (haveEntity && fSettings.picture) {
		place::Image image = place::PickImage(entity, place::IsAdministrative(hit));
		BNode node(&ref);
		attr_info info;
		const bool has = node.GetAttrInfo(THUMBNAIL_ATTR_NAME, &info) == B_OK && info.size > 0;
		if (!image.file.empty() && (fOverwrite || !has)) {
			std::string body, data;
			place::Image found;
			if (Get(place::ImageInfoUrl(image.file, kThumbnailWidth), kWikimedia, &body) == B_OK && place::ParseImageInfo(body, &found)
					&& GetImage(found.thumbnailUrl, &data) == B_OK && !data.empty()) {
				found.kind = image.kind;
				found.file = image.file;
				if (place::WriteImage(ref, data, found, fOverwrite) == B_OK)
					pictured = image.kind;
				else
					printf("error writing the picture to the thumbnail of %s.\n", ref.name);
			} else {
				printf("error fetching the picture '%s', skipping it.\n", image.file.c_str());
			}
		}
	}

	BString summary;
	summary << hit.name.c_str();
	const std::string kind = KindOf(hit);
	if (!kind.empty())
		summary << " - " << kind.c_str();
	if (!pictured.empty())
		summary << " - " << pictured.c_str();
	printf("%s\n", summary.String());
	if (!fAuto)
		sensei::Notify("SEN SUESS", "Place enriched", summary.String(), true, &ref);
	reply->AddString(sen::key::kDetail, summary.String());
	return B_OK;
}
