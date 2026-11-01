/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperPeople.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "PaperApi.h"
#include "PaperString.h"

namespace paperboy {

namespace {

using nlohmann::json;

std::string
Str(const json& object, const char* key)
{
	if (!object.is_object())
		return "";
	auto it = object.find(key);
	return (it != object.end() && it->is_string()) ? it->get<std::string>() : "";
}

const json&
Child(const json& object, const char* key)
{
	static const json kNull;
	if (!object.is_object())
		return kNull;
	auto it = object.find(key);
	return it != object.end() ? *it : kNull;
}

/** `{"value": "..."}`, as ORCID wraps its strings */
std::string
Value(const json& object, const char* key)
{
	return Str(Child(object, key), "value");
}

void
AddSameAs(PersonData* person, const std::string& iri)
{
	if (!iri.empty() && std::find(person->sameAs.begin(), person->sameAs.end(), iri) == person->sameAs.end())
		person->sameAs.push_back(iri);
}

bool
IsWikidataId(const std::string& id)
{
	return id.size() > 1 && id[0] == 'Q' && std::all_of(id.begin() + 1, id.end(), [](unsigned char c) { return std::isdigit(c); });
}

}	// namespace


std::string
OrcidUrl(const std::string& orcid, const std::string& part)
{
	return "https://pub.orcid.org/v3.0/" + UrlEncode(orcid) + "/" + part;
}


std::string
WikidataSearchUrl(const std::string& orcid)
{
	return "https://www.wikidata.org/w/api.php?action=query&list=search&srsearch=" + UrlEncode("haswbstatement:P496=" + orcid) + "&format=json";
}


std::string
WikidataEntityUrl(const std::string& wikidataId)
{
	return "https://www.wikidata.org/w/api.php?action=wbgetentities&ids=" + UrlEncode(wikidataId)
		+ "&props=claims%7Cdescriptions%7Clabels%7Csitelinks&languages=en&sitefilter=enwiki&format=json";
}


std::string
CommonsImageUrl(const std::string& photoFile, int width)
{
	return "https://commons.wikimedia.org/w/api.php?action=query&titles=" + UrlEncode("File:" + photoFile)
		+ "&prop=imageinfo&iiprop=url%7Cextmetadata&iiurlwidth=" + std::to_string(width) + "&format=json";
}


bool
ParseOrcidPerson(const std::string& body, PersonData* person)
{
	json answer = json::parse(body, nullptr, false);
	const json& name = Child(answer, "name");
	if (!answer.is_object() || !name.is_object())
		return false;

	person->givenName = Value(name, "given-names");
	person->familyName = Value(name, "family-name");
	std::string full = Value(name, "credit-name");
	if (full.empty())
		full = person->givenName + (person->givenName.empty() || person->familyName.empty() ? "" : " ") + person->familyName;
	if (!full.empty())
		person->name = full;

	const std::string biography = Str(Child(answer, "biography"), "content");
	if (!biography.empty())
		person->biography = biography;

	const json& urls = Child(Child(answer, "researcher-urls"), "researcher-url");
	if (urls.is_array() && person->homepage.empty()) {
		for (const json& url : urls) {
			const std::string address = Value(url, "url");
			if (address.compare(0, 4, "http") == 0) {
				person->homepage = address;
				break;
			}
		}
	}

	// "path" is the address of the record ("/0000-0002-9322-3515/person"): the iD in it is the one that is wanted
	const std::string path = Str(answer, "path");
	for (size_t at = 0; at + 19 <= path.size(); at++) {
		const std::string id = path.substr(at, 19);
		bool fits = true;
		for (size_t i = 0; i < id.size() && fits; i++)
			fits = (i == 4 || i == 9 || i == 14) ? id[i] == '-' : (std::isdigit((unsigned char) id[i]) || (i == 18 && id[i] == 'X'));
		if (fits) {
			person->orcid = id;
			AddSameAs(person, "https://orcid.org/" + id);
			break;
		}
	}
	return true;
}


bool
ParseOrcidEmployments(const std::string& body, PersonData* person)
{
	json answer = json::parse(body, nullptr, false);
	const json& groups = Child(answer, "affiliation-group");
	if (!answer.is_object() || !groups.is_array())
		return false;

	std::string current, first;
	for (const json& group : groups) {
		const json& summaries = Child(group, "summaries");
		if (!summaries.is_array())
			continue;
		for (const json& entry : summaries) {
			const json& employment = Child(entry, "employment-summary");
			const std::string organization = Str(Child(employment, "organization"), "name");
			if (organization.empty())
				continue;
			const std::string role = Str(employment, "role-title");
			const std::string text = role.empty() ? organization : role + ", " + organization;
			if (first.empty())
				first = text;
			if (current.empty() && Child(employment, "end-date").is_null())
				current = text;
		}
	}
	person->jobTitle = current.empty() ? first : current;
	return true;
}


bool
ParseWikidataSearch(const std::string& body, std::string* wikidataId)
{
	json answer = json::parse(body, nullptr, false);
	const json& hits = Child(Child(answer, "query"), "search");
	if (!answer.is_object() || !hits.is_array())
		return false;
	for (const json& hit : hits) {
		const std::string title = Str(hit, "title");
		if (IsWikidataId(title)) {
			*wikidataId = title;
			return true;
		}
	}
	return false;
}


bool
ParseWikidataEntity(const std::string& body, const std::string& wikidataId, PersonData* person)
{
	json answer = json::parse(body, nullptr, false);
	const json& entity = Child(Child(answer, "entities"), wikidataId.c_str());
	if (!entity.is_object() || entity.contains("missing"))
		return false;

	person->wikidataId = wikidataId;
	AddSameAs(person, "https://www.wikidata.org/wiki/" + wikidataId);
	const std::string wikipedia = Str(Child(Child(entity, "sitelinks"), "enwiki"), "title");
	if (!wikipedia.empty()) {
		std::string page = wikipedia;
		std::replace(page.begin(), page.end(), ' ', '_');
		AddSameAs(person, "https://en.wikipedia.org/wiki/" + UrlEncode(page, "_(),:'"));
	}

	if (person->biography.empty())
		person->biography = Str(Child(Child(entity, "descriptions"), "en"), "value");
	if (person->name.empty())
		person->name = Str(Child(Child(entity, "labels"), "en"), "value");

	const json& claims = Child(entity, "claims");
	auto firstValue = [&](const char* property) -> const json& {
		static const json kNull;
		const json& list = Child(claims, property);
		if (!list.is_array() || list.empty())
			return kNull;
		return Child(Child(Child(list.front(), "mainsnak"), "datavalue"), "value");
	};

	const json& image = firstValue("P18");
	if (image.is_string())
		person->photoFile = image.get<std::string>();

	const json& born = firstValue("P569");
	if (person->birthDate.empty() && born.is_object()) {
		// "+1964-03-05T00:00:00Z": the precision says how much of it is known (11: the day, 9: the year)
		std::string time = Str(born, "time");
		const int precision = born.contains("precision") && born["precision"].is_number() ? born["precision"].get<int>() : 0;
		if (!time.empty() && time[0] == '+')
			time.erase(0, 1);
		if (time.size() >= 10 && precision >= 11)
			person->birthDate = time.substr(0, 10);
		else if (time.size() >= 4 && precision >= 9)
			person->birthDate = time.substr(0, 4);
	}

	const json& site = firstValue("P856");
	if (person->homepage.empty() && site.is_string())
		person->homepage = site.get<std::string>();
	return true;
}


bool
ParseCommonsImage(const std::string& body, PersonData* person)
{
	json answer = json::parse(body, nullptr, false);
	const json& pages = Child(Child(answer, "query"), "pages");
	if (!pages.is_object() || pages.empty())
		return false;

	const json& info = Child(pages.begin().value(), "imageinfo");
	if (!info.is_array() || info.empty())
		return false;
	const json& file = info.front();

	const std::string url = !Str(file, "thumburl").empty() ? Str(file, "thumburl") : Str(file, "url");
	if (url.empty())
		return false;
	person->photoUrl = url;
	person->photoPage = Str(file, "descriptionurl");

	const json& meta = Child(file, "extmetadata");
	person->photoLicense = Value(meta, "LicenseShortName");
	person->photoCredit = StripTags(Value(meta, "Artist"));
	if (person->photoCredit.empty())
		person->photoCredit = StripTags(Value(meta, "Credit"));
	return true;
}

}	// namespace paperboy
