/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "MovieTmdb.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

#include <nlohmann/json.hpp>

namespace movie {

namespace {

const char* const kApi = "https://api.themoviedb.org/3";
const char* const kImages = "https://image.tmdb.org/t/p/";

using Json = nlohmann::json;

std::string
Str(const Json& json, const char* key)
{
	auto it = json.find(key);
	return it != json.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int64_t
Int(const Json& json, const char* key)
{
	auto it = json.find(key);
	return it != json.end() && it->is_number() ? it->get<int64_t>() : 0;
}

double
Num(const Json& json, const char* key)
{
	auto it = json.find(key);
	return it != json.end() && it->is_number() ? it->get<double>() : 0;
}

int
YearOf(const std::string& date)
{
	if (date.size() < 4)
		return 0;
	for (size_t i = 0; i < 4; i++) {
		if (!std::isdigit((unsigned char)date[i]))
			return 0;
	}
	return std::stoi(date.substr(0, 4));
}

/** letters and digits in lower case, nothing else: titles compared as a person reads them */
std::string
Plain(const std::string& text)
{
	std::string plain;
	for (unsigned char c : text) {
		if (std::isalnum(c))
			plain += (char)std::tolower(c);
		else if (c >= 0x80)
			plain += (char)c;		// (letters of other alphabets stay: Amélie is not Amlie)
	}
	return plain;
}

void
AddUnique(std::vector<std::string>* list, const std::string& value)
{
	if (!value.empty() && std::find(list->begin(), list->end(), value) == list->end())
		list->push_back(value);
}

bool
Parse(const std::string& text, Json* json)
{
	*json = Json::parse(text, nullptr, false);
	return !json->is_discarded() && json->is_object();
}

}	// namespace


std::string
PercentEncode(const std::string& text)
{
	static const char* hex = "0123456789ABCDEF";
	std::string encoded;
	for (unsigned char c : text) {
		if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
			encoded += (char)c;
		} else {
			encoded += '%';
			encoded += hex[c >> 4];
			encoded += hex[c & 15];
		}
	}
	return encoded;
}


std::string
SearchUrl(const std::string& key, const std::string& language, const std::string& title, int year)
{
	std::string url = std::string(kApi) + "/search/movie?api_key=" + PercentEncode(key) + "&query=" + PercentEncode(title) + "&include_adult=false";
	if (!language.empty())
		url += "&language=" + PercentEncode(language);
	if (year > 0)
		url += "&year=" + std::to_string(year);
	return url;
}


std::string
DetailsUrl(const std::string& key, const std::string& language, int64_t id)
{
	std::string url = std::string(kApi) + "/movie/" + std::to_string(id) + "?api_key=" + PercentEncode(key) + "&append_to_response=credits,external_ids";
	if (!language.empty())
		url += "&language=" + PercentEncode(language);
	return url;
}


std::string
PersonUrl(const std::string& key, const std::string& language, int64_t id)
{
	std::string url = std::string(kApi) + "/person/" + std::to_string(id) + "?api_key=" + PercentEncode(key) + "&append_to_response=external_ids";
	if (!language.empty())
		url += "&language=" + PercentEncode(language);
	return url;
}


std::string
ImageUrl(const std::string& size, const std::string& path)
{
	if (path.empty())
		return std::string();
	return std::string(kImages) + size + (path[0] == '/' ? "" : "/") + path;
}


std::string
WithoutKey(const std::string& url)
{
	const size_t at = url.find("api_key=");
	if (at == std::string::npos)
		return url;
	size_t end = url.find('&', at);
	if (end == std::string::npos)
		end = url.size();
	return url.substr(0, at) + "api_key=***" + url.substr(end);
}


bool
ParseSearch(const std::string& json, std::vector<SearchHit>* hits)
{
	Json root;
	if (!Parse(json, &root) || !root.contains("results") || !root["results"].is_array())
		return false;
	hits->clear();
	for (const Json& item : root["results"]) {
		if (!item.is_object() || Int(item, "id") == 0)
			continue;
		SearchHit hit;
		hit.id = Int(item, "id");
		hit.title = Str(item, "title");
		hit.originalTitle = Str(item, "original_title");
		hit.year = YearOf(Str(item, "release_date"));
		hit.overview = Str(item, "overview");
		hit.posterPath = Str(item, "poster_path");
		hit.popularity = Num(item, "popularity");
		hit.voteCount = (int)Int(item, "vote_count");
		hits->push_back(hit);
	}
	return true;
}


std::vector<Credit>
KeyCrew(const std::vector<Credit>& crew)
{
	// the jobs of TMDB and the role that each is
	static const std::pair<const char*, const char*> kJobs[] = {
		{"Director", "Director"},
		{"Screenplay", "Writer"}, {"Writer", "Writer"}, {"Story", "Writer"}, {"Novel", "Writer"}, {"Screenstory", "Writer"},
		{"Producer", "Producer"},
		{"Original Music Composer", "Composer"}, {"Composer", "Composer"},
		{"Director of Photography", "Cinematographer"},
		{"Editor", "Editor"},
	};
	static const char* const kOrder[] = {"Director", "Writer", "Producer", "Composer", "Cinematographer", "Editor"};

	std::vector<Credit> key;
	for (const char* role : kOrder) {
		int position = 0;
		for (const Credit& credit : crew) {
			std::string mapped;
			for (const auto& job : kJobs) {
				if (credit.role == job.first)
					mapped = job.second;
			}
			if (mapped != role)
				continue;
			const bool known = std::any_of(key.begin(), key.end(), [&](const Credit& each) { return each.personId == credit.personId && each.role == role; });
			if (known)
				continue;
			Credit made = credit;
			made.role = role;
			made.position = ++position;
			key.push_back(made);
		}
	}
	return key;
}


bool
ParseDetails(const std::string& json, Details* details)
{
	Json root;
	if (!Parse(json, &root) || Int(root, "id") == 0)
		return false;

	*details = Details();
	details->id = Int(root, "id");
	details->title = Str(root, "title");
	details->originalTitle = Str(root, "original_title");
	details->releaseDate = Str(root, "release_date");
	details->runtime = (int)Int(root, "runtime");
	details->overview = Str(root, "overview");
	details->posterPath = Str(root, "poster_path");
	details->imdbId = Str(root, "imdb_id");

	if (root.contains("genres") && root["genres"].is_array()) {
		for (const Json& genre : root["genres"])
			AddUnique(&details->genres, Str(genre, "name"));
	}
	AddUnique(&details->languages, Str(root, "original_language"));
	if (root.contains("spoken_languages") && root["spoken_languages"].is_array()) {
		for (const Json& language : root["spoken_languages"])
			AddUnique(&details->languages, Str(language, "iso_639_1"));
	}
	if (root.contains("production_countries") && root["production_countries"].is_array()) {
		for (const Json& country : root["production_countries"])
			AddUnique(&details->countries, Str(country, "iso_3166_1"));
	}
	if (root.contains("external_ids") && root["external_ids"].is_object()) {
		const Json& ids = root["external_ids"];
		if (details->imdbId.empty())
			details->imdbId = Str(ids, "imdb_id");
		details->wikidataId = Str(ids, "wikidata_id");
	}

	if (root.contains("credits") && root["credits"].is_object()) {
		const Json& credits = root["credits"];
		if (credits.contains("cast") && credits["cast"].is_array()) {
			int position = 0;
			for (const Json& item : credits["cast"]) {
				Credit credit;
				credit.personId = Int(item, "id");
				credit.name = Str(item, "name");
				if (credit.personId == 0 || credit.name.empty())
					continue;
				credit.role = "Actor";
				credit.character = Str(item, "character");
				credit.position = item.contains("order") && item["order"].is_number() ? (int)Int(item, "order") + 1 : ++position;
				credit.profilePath = Str(item, "profile_path");
				details->cast.push_back(credit);
			}
			std::stable_sort(details->cast.begin(), details->cast.end(), [](const Credit& a, const Credit& b) { return a.position < b.position; });
		}
		if (credits.contains("crew") && credits["crew"].is_array()) {
			std::vector<Credit> crew;
			for (const Json& item : credits["crew"]) {
				Credit credit;
				credit.personId = Int(item, "id");
				credit.name = Str(item, "name");
				if (credit.personId == 0 || credit.name.empty())
					continue;
				credit.role = Str(item, "job");		// (the job of TMDB; KeyCrew() makes the role of it)
				credit.profilePath = Str(item, "profile_path");
				crew.push_back(credit);
			}
			details->crew = KeyCrew(crew);
		}
	}
	return true;
}


bool
ParsePerson(const std::string& json, PersonDetails* person)
{
	Json root;
	if (!Parse(json, &root) || Int(root, "id") == 0)
		return false;
	*person = PersonDetails();
	person->id = Int(root, "id");
	person->name = Str(root, "name");
	person->biography = Str(root, "biography");
	person->birthday = Str(root, "birthday");
	person->imdbId = Str(root, "imdb_id");
	person->profilePath = Str(root, "profile_path");
	if (root.contains("external_ids") && root["external_ids"].is_object()) {
		if (person->imdbId.empty())
			person->imdbId = Str(root["external_ids"], "imdb_id");
		person->wikidataId = Str(root["external_ids"], "wikidata_id");
	}
	return true;
}


std::string
ParseError(const std::string& json)
{
	Json root;
	if (!Parse(json, &root))
		return std::string();
	return Str(root, "status_message");
}


int64_t
TmdbIdFrom(const std::string& links, const std::string& kind)
{
	const std::string marker = "themoviedb.org/" + kind + "/";
	size_t at = links.find(marker);
	if (at == std::string::npos)
		return 0;
	at += marker.size();
	size_t end = at;
	while (end < links.size() && std::isdigit((unsigned char)links[end]))
		end++;
	if (end == at || end - at > 12)
		return 0;
	return std::stoll(links.substr(at, end - at));
}


std::string
DurationIso(int minutes)
{
	if (minutes <= 0)
		return std::string();
	std::string text = "PT";
	if (minutes >= 60)
		text += std::to_string(minutes / 60) + "H";
	if (minutes % 60 != 0)
		text += std::to_string(minutes % 60) + "M";
	return text;
}


std::vector<std::string>
SameAs(const Details& details)
{
	std::vector<std::string> links;
	links.push_back("https://www.themoviedb.org/movie/" + std::to_string(details.id));
	if (!details.imdbId.empty())
		links.push_back("https://www.imdb.com/title/" + details.imdbId + "/");
	if (!details.wikidataId.empty())
		links.push_back("https://www.wikidata.org/entity/" + details.wikidataId);
	links.push_back("https://letterboxd.com/tmdb/" + std::to_string(details.id));
	return links;
}


std::vector<std::string>
SameAs(const PersonDetails& person)
{
	std::vector<std::string> links;
	links.push_back("https://www.themoviedb.org/person/" + std::to_string(person.id));
	if (!person.imdbId.empty())
		links.push_back("https://www.imdb.com/name/" + person.imdbId + "/");
	if (!person.wikidataId.empty())
		links.push_back("https://www.wikidata.org/entity/" + person.wikidataId);
	return links;
}


bool
TitleMatches(const SearchHit& hit, const std::string& wanted)
{
	const std::string want = Plain(wanted);
	if (want.empty())
		return true;
	for (const std::string& title : {Plain(hit.title), Plain(hit.originalTitle)}) {
		if (!title.empty() && (title == want || title.find(want) != std::string::npos || want.find(title) != std::string::npos))
			return true;
	}
	return false;
}


float
ScoreHit(const SearchHit& hit, int position, int count, const std::string& wantedTitle, int wantedYear)
{
	const std::string want = Plain(wantedTitle);
	float title = 0;
	for (const std::string& each : {Plain(hit.title), Plain(hit.originalTitle)}) {
		if (each.empty() || want.empty())
			continue;
		if (each == want)
			title = std::max(title, 1.0f);
		else if (each.find(want) != std::string::npos || want.find(each) != std::string::npos)
			title = std::max(title, 0.6f);
	}
	if (want.empty())
		title = 0.5f;

	float year = 0.5f;
	if (wantedYear > 0 && hit.year > 0)
		year = hit.year == wantedYear ? 1.0f : (std::abs(hit.year - wantedYear) == 1 ? 0.5f : 0.0f);
	else if (wantedYear > 0)
		year = 0.2f;

	const float rank = count > 1 ? 1.0f - (float)position / (float)count : 1.0f;
	// (how well known: the votes, on a logarithmic scale up to a few thousand)
	const float known = std::min(1.0f, (float)(std::log10(1.0 + hit.voteCount) / 4.0));
	return std::clamp(0.40f * title + 0.25f * year + 0.15f * rank + 0.20f * known, 0.0f, 1.0f);
}


std::vector<std::string>
NamesWithRole(const std::vector<Credit>& credits, const std::string& role)
{
	std::vector<const Credit*> found;
	for (const Credit& credit : credits) {
		if (credit.role == role && !credit.name.empty())
			found.push_back(&credit);
	}
	std::stable_sort(found.begin(), found.end(), [](const Credit* a, const Credit* b) { return a->position < b->position; });
	std::vector<std::string> names;
	for (const Credit* credit : found) {
		if (std::find(names.begin(), names.end(), credit->name) == names.end())
			names.push_back(credit->name);
	}
	return names;
}

}	// namespace movie
