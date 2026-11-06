/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "App.h"

#include <Entry.h>
#include <Node.h>
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
#include <sen/SenOntoMovies.h>
#include <sen/SenPluginSettings.h>
#include <sen/SenSettingsPath.h>
#include <sen/Sensei.h>

#include "../../common/MappingUtil.h"
#include "../../common/Notice.h"
#include "MovieName.h"
#include "MoviePeople.h"
#include "MovieWriter.h"

static const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.movie-enricher";
static const char* kKeyVariable = "SEN_TMDB_KEY";

namespace {

/** the people of a movie from TMDB: their details and their photos */
class TmdbPeople : public movie::PersonSource {
public:
	TmdbPeople(BaseEnricher* http, const std::string& key, const std::string& language)
		:
		fHttp(http),
		fKey(key),
		fLanguage(language)
	{
	}

	virtual bool Fetch(int64_t id, movie::PersonDetails* person)
	{
		std::string body;
		BUrl url(movie::PersonUrl(fKey, fLanguage, id).c_str(), false);
		return fHttp->FetchRemoteContent(url, &body) == B_OK && movie::ParsePerson(body, person);
	}

	virtual bool FetchPhoto(const std::string& path, std::string* image)
	{
		BUrl url(movie::ImageUrl("w185", path).c_str(), false);
		return fHttp->FetchRemoteContent(url, image) == B_OK;
	}

private:
	BaseEnricher*	fHttp;
	std::string		fKey;
	std::string		fLanguage;
};

}	// namespace


App::App()
	:
	BApplication(kApplicationSignature),
	fMapper(new MappingUtil()),
	fHttp(NULL),
	fDebug(false),
	fKeyGiven(false),
	fLanguageGiven(false),
	fOverwrite(true),
	fAuto(false),
	fExitCode(0)
{
	_ReloadSettings();
}


void
App::_ReloadSettings()
{
	const std::string key = fSettings.apiKey, language = fSettings.language;
	fSettings = movie::Settings();
	movie::LoadSettings(&fSettings);
	// (what all plugins share is set in the settings of SEN; `--auto` wins over it for one run)
	const sen::plugins::SharedSettings shared = sen::plugins::LoadSharedSettings();
	fSettings.choose = shared.askIfSeveral;
	fSettings.maxCast = shared.maxEntities;
	const char* variable = getenv(kKeyVariable);
	if (variable != NULL && variable[0] != '\0')
		fSettings.apiKey = variable;
	if (fKeyGiven)
		fSettings.apiKey = key;
	if (fLanguageGiven)
		fSettings.language = language;
}


App::~App()
{
	delete fHttp;
	delete fMapper;
}


int
main()
{
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
		<< "Usage: SenMovieEnricher [options] <file>...\n"
		<< "Looks a movie up at The Movie Database: writes its attributes and the poster to the file, and makes people of the director, the crew and the cast.\n"
		<< "This product uses the TMDB API but is not endorsed or certified by TMDB.\n\n"
		<< "  -a, --auto         if several movies are found, take the best without asking\n"
		<< "  -w, --wipe         replace attributes that the file has (the default for a request of the menu)\n"
		<< "  -k, --key <key>    the TMDB API key (v3) or read access token (v4), or " << kKeyVariable << ", or the setting api_key\n"
		<< "  -l, --language <l> the language of the answers (en-US, de-DE, ...)\n"
		<< "      --debug        print what is done\n\n"
		<< "Settings: the Settings window of SEN (Plugins, Enrich), kept in ~/config/settings/sen/akari.settings.\n";
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
		} else if (strcmp(arg, "--debug") == 0) {
			fDebug = true;
		} else if ((strcmp(arg, "-k") == 0 || strcmp(arg, "--key") == 0) && i + 1 < argc) {
			fSettings.apiKey = argv[++i];
			fKeyGiven = true;
		} else if ((strcmp(arg, "-l") == 0 || strcmp(arg, "--language") == 0) && i + 1 < argc) {
			fSettings.language = argv[++i];
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
	_ReloadSettings();		// (this instance may have been started before the user set the key)
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
	sensei::Notify("SEN Akari", "Movie not enriched", message, false);
	printf("%s\n", message);
}


status_t
App::Get(const std::string& url, std::string* body)
{
	BUrl address(url.c_str(), false);
	status_t result = fHttp->FetchRemoteContent(address, body);
	if (result != B_OK) {
		const int status = fHttp->LastHttpStatus();
		if (status == 401 || status == 403)
			return B_PERMISSION_DENIED;
		return B_ERROR;
	}
	return B_OK;
}


/** One answer as it is shown in the list of sen::ChooseCandidate(): title, year and original title, the start of the description, the source. */
static sen::Candidate
Describe(const movie::SearchHit& hit, float score)
{
	sen::Candidate candidate;
	candidate.title = hit.title.c_str();
	if (hit.year > 0)
		candidate.subtitle << hit.year;
	if (!hit.originalTitle.empty() && hit.originalTitle != hit.title) {
		if (!candidate.subtitle.IsEmpty())
			candidate.subtitle << " - ";
		candidate.subtitle << hit.originalTitle.c_str();
	}
	BString overview(hit.overview.c_str());
	if (overview.CountChars() > 110) {
		overview.TruncateChars(110);
		overview << B_UTF8_ELLIPSIS;
	}
	candidate.detail = overview;
	candidate.source = "TMDB";
	candidate.score = score;
	return candidate;
}


status_t
App::ChooseMovie(const std::vector<movie::SearchHit>& hits, const std::string& title, int year, int64_t* id)
{
	// the ones with the title that was asked for (an answer of another movie is no help, however well TMDB ranks it)
	std::vector<size_t> matching;
	for (size_t i = 0; i < hits.size(); i++) {
		if (movie::TitleMatches(hits[i], title))
			matching.push_back(i);
	}

	std::vector<sen::Candidate> candidates;
	std::vector<size_t> shown;
	const size_t count = std::min<size_t>(hits.size(), 10);
	size_t best = count;
	float bestScore = -1;
	for (size_t i = 0; i < count; i++) {
		const float score = movie::ScoreHit(hits[i], (int)i, (int)hits.size(), title, year);
		candidates.push_back(Describe(hits[i], score));
		shown.push_back(i);
		if ((!fAuto || movie::TitleMatches(hits[i], title)) && score > bestScore) {
			best = i;
			bestScore = score;
		}
	}
	if (candidates.empty())
		return B_ENTRY_NOT_FOUND;

	if (fAuto) {
		if (best == count) {
			printf("got %zu movies, none of them called '%s'.\n", hits.size(), title.c_str());
			return B_ENTRY_NOT_FOUND;
		}
		printf("got %zu movies, taking the best: '%s'.\n", hits.size(), hits[best].title.c_str());
		*id = hits[best].id;
		return B_OK;
	}
	// one answer and it is the movie that was asked for: nothing to ask
	if (hits.size() == 1 && matching.size() == 1) {
		*id = hits[0].id;
		return B_OK;
	}

	printf("got %zu movies, asking the user...\n", hits.size());
	const int32 picked = sen::ChooseCandidate("Choose a movie", "The Movie Database found several movies. Which one is yours?", candidates,
		sen::LoadPluginLogo(kApplicationSignature));
	if (picked < 0)
		return B_CANCELED;
	*id = hits[shown[picked]].id;
	return B_OK;
}


status_t
App::FindMovie(const entry_ref& file, int64_t* id)
{
	BNode node(&file);
	BString text;

	// a movie that was enriched before has its TMDB link: no search, no question
	if (node.ReadAttrString(sen::onto::movies::attr::kSameAs, &text) == B_OK) {
		const int64_t known = movie::TmdbIdFrom(text.String(), "movie");
		if (known > 0) {
			*id = known;
			return B_OK;
		}
	}

	// what is asked for: the title that the file has, else the one in its name; the year of the release, else the one in its name
	movie::Guess guess = movie::GuessFromFileName(file.name);
	std::string title = guess.title;
	if (node.ReadAttrString(sen::onto::movies::attr::kTitle, &text) == B_OK && !text.IsEmpty())
		title = text.String();
	int year = guess.year;
	time_t released = 0;
	if (node.ReadAttr(sen::onto::movies::attr::kReleaseDate, B_TIME_TYPE, 0, &released, sizeof(released)) == (ssize_t)sizeof(released)) {
		const std::string date = sen::settings::FormatTime((int64_t)released);
		if (date.size() >= 4)
			year = atoi(date.substr(0, 4).c_str());
	}
	fWanted = title;
	if (title.empty())
		return B_ENTRY_NOT_FOUND;

	std::vector<movie::SearchHit> hits;
	for (int attempt = 0; attempt < 2; attempt++) {
		if (attempt == 1 && year == 0)
			break;			// (a year that is wrong finds nothing: the title alone)
		std::string body;
		status_t result = Get(movie::SearchUrl(fSettings.apiKey, fSettings.language, title, attempt == 0 ? year : 0), &body);
		if (result != B_OK)
			return result;
		if (!movie::ParseSearch(body, &hits))
			return B_ERROR;
		if (!hits.empty())
			break;
	}
	if (hits.empty())
		return B_ENTRY_NOT_FOUND;
	return ChooseMovie(hits, title, year, id);
}


/**
 * The work of the plugin: find the movie, write what TMDB says of it to the attributes of the file and the poster as its thumbnail, and make people of the crew and the cast
 * with a Contribution each. The result is that of the MOVIE: that a person could not be made does not undo it.
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

	if (fSettings.apiKey.empty()) {
		ShowError("Set the API key of The Movie Database in the settings of SEN (Settings, Plugins, SEN Akari). The key is free: themoviedb.org/settings/api.");
		return B_NOT_ALLOWED;
	}

	if (!fAuto)		// (the lookup takes a while: say that it has begun; a script that asked for --auto is not told, as at the end)
		sensei::NotifyStarted("SEN Akari", "Enriching movie", ref.name);

	delete fHttp;
	fHttp = new BaseEnricher(&ref, fMapper);
	if (movie::IsReadToken(fSettings.apiKey))
		fHttp->SetBearerToken(fSettings.apiKey.c_str());

	int64_t id = 0;
	status_t result = FindMovie(ref, &id);
	if (result == B_CANCELED)
		return result;
	if (result == B_PERMISSION_DENIED) {
		ShowError("The Movie Database does not accept the API key. Check it in the settings of SEN.");
		return result;
	}
	if (result == B_ENTRY_NOT_FOUND) {
		BString text("The Movie Database has no movie called \"");
		text << fWanted.c_str() << "\". Set the title of the file (dc:title), or name the file after the movie.";
		ShowError(text.String());
		return result;
	}
	if (result != B_OK) {
		ShowError("Failed to look up the movie.");
		return result;
	}

	std::string body;
	movie::Details details;
	result = Get(movie::DetailsUrl(fSettings.apiKey, fSettings.language, id), &body);
	if (result == B_OK && !movie::ParseDetails(body, &details))
		result = B_ERROR;
	if (result != B_OK) {
		ShowError(result == B_PERMISSION_DENIED ? "The Movie Database does not accept the API key. Check it in the settings of SEN." : "Failed to look up the movie.");
		return result;
	}

	result = movie::WriteMovie(ref, details, fOverwrite);
	if (result != B_OK) {
		ShowError("Failed to write the metadata to the file.");
		return result;
	}

	// the poster, like the cover of a book
	if (!details.posterPath.empty()) {
		std::string image;
		if (Get(movie::ImageUrl("w500", details.posterPath), &image) == B_OK && !image.empty()) {
			if (movie::WritePoster(ref, image, fOverwrite) != B_OK)
				printf("error writing the poster to the thumbnail of %s.\n", ref.name);
		} else {
			printf("error fetching the poster, skipping it.\n");
		}
	}

	// the people: the director and the crew, the cast (the first ones)
	std::vector<movie::Credit> credits;
	if (fSettings.crew)
		credits.insert(credits.end(), details.crew.begin(), details.crew.end());
	if (fSettings.cast) {
		for (size_t i = 0; i < details.cast.size() && (int)i < fSettings.maxCast; i++)
			credits.push_back(details.cast[i]);
	}
	BString summary;
	summary << details.title.c_str();
	if (!credits.empty() && !sen::plugins::ExtractEntitiesIntoFiles()) {
		// (the settings of SEN say: no entities of their own, only the names in the attributes of the movie)
		movie::WriteNames(ref, credits, fOverwrite);
		printf("entities are kept in the attributes: the names of the director and the cast written, no people made.\n");
	} else if (!credits.empty()) {
		TmdbPeople source(fHttp, fSettings.apiKey, fSettings.language);
		movie::PeopleResult people = movie::MakePeople(ref, credits, &source, fOverwrite, fDebug);
		printf("%d people (%d new), %d contributions, %d skipped.\n", people.people, people.made, people.contributions, people.skipped);
		summary << " - " << people.people << " people";
	}
	if (!fAuto)
		sensei::Notify("SEN Akari", "Movie enriched", summary.String(), true, &ref);
	reply->AddString(sen::key::kDetail, summary.String());
	return B_OK;
}
