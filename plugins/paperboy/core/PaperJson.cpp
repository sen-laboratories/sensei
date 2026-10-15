/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperJson.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "PaperAbstract.h"
#include "PaperDoi.h"

using nlohmann::json;

namespace paperboy {

namespace {

constexpr double kMinKeywordScore = 0.3;

/** the string at key, "" if it is missing or null */
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

bool
Bool(const json& object, const char* key)
{
	const json& value = Child(object, key);
	return value.is_boolean() && value.get<bool>();
}

double
Number(const json& object, const char* key, double fallback)
{
	const json& value = Child(object, key);
	return value.is_number() ? value.get<double>() : fallback;
}

std::string
StripOrcid(const std::string& orcid)
{
	size_t slash = orcid.rfind('/');
	return slash == std::string::npos ? orcid : orcid.substr(slash + 1);
}

bool
HasUrl(const std::vector<Location>& locations, const Location& candidate)
{
	return std::any_of(locations.begin(), locations.end(),
		[&](const Location& l) { return l.landingUrl == candidate.landingUrl && l.pdfUrl == candidate.pdfUrl; });
}

/** a location of OpenAlex, which is also the layout of the old "best_oa_location" of the work */
Location
OpenAlexLocation(const json& loc, bool best)
{
	Location location;
	location.landingUrl = Str(loc, "landing_page_url");
	location.pdfUrl = Str(loc, "pdf_url");
	location.version = Str(loc, "version");
	location.license = Str(loc, "license");
	location.hostType = Str(Child(loc, "source"), "type");
	location.source = "openalex";
	location.isBest = best;
	return location;
}

/** a location of Unpaywall */
Location
UnpaywallLocation(const json& loc, bool best)
{
	Location location;
	location.landingUrl = Str(loc, "url_for_landing_page");
	if (location.landingUrl.empty())
		location.landingUrl = Str(loc, "url");
	location.pdfUrl = Str(loc, "url_for_pdf");
	location.version = Str(loc, "version");
	location.license = Str(loc, "license");
	location.hostType = Str(loc, "host_type");
	location.source = "unpaywall";
	location.isBest = best;
	return location;
}

void
ParseKeywords(const json& work, Paper* paper)
{
	// "keywords" replaces the concepts (deprecated), which stay as the fallback; the topics are the last one
	for (const char* field : { "keywords", "concepts" }) {
		const json& list = Child(work, field);
		if (!list.is_array())
			continue;
		for (const json& entry : list) {
			std::string name = Str(entry, "display_name");
			if (!name.empty() && Number(entry, "score", 0) >= kMinKeywordScore)
				paper->keywords.push_back(name);
		}
		if (!paper->keywords.empty())
			return;
	}
	const json& topics = Child(work, "topics");
	if (topics.is_array()) {
		for (const json& entry : topics) {
			std::string name = Str(entry, "display_name");
			if (!name.empty() && paper->keywords.size() < 3)
				paper->keywords.push_back(name);
		}
	}
}

}	// namespace


std::string
ShortOpenAlexId(const std::string& id)
{
	static const std::string kPrefix = "https://openalex.org/";
	return id.compare(0, kPrefix.size(), kPrefix) == 0 ? id.substr(kPrefix.size()) : id;
}


bool
ParseOpenAlexWork(const std::string& body, Paper* paper)
{
	json work = json::parse(body, nullptr, false);
	if (work.is_discarded() || !work.is_object() || Str(work, "id").empty())
		return false;

	paper->fromOpenAlex = true;
	paper->openAlexId = ShortOpenAlexId(Str(work, "id"));
	std::string doi = NormalizeDoi(Str(work, "doi"));
	if (!doi.empty())
		paper->doi = doi;

	paper->title = Str(work, "title");
	if (paper->title.empty())
		paper->title = Str(work, "display_name");
	paper->type = Str(work, "type");
	paper->date = Str(work, "publication_date");
	paper->year = (int) Number(work, "publication_year", 0);
	if (paper->date.empty() && paper->year > 0)
		paper->date = std::to_string(paper->year);
	paper->language = Str(work, "language");
	paper->citedByCount = (int) Number(work, "cited_by_count", -1);

	const json& source = Child(Child(work, "primary_location"), "source");
	paper->venue = Str(source, "display_name");
	paper->publisher = Str(source, "host_organization_name");

	const json& authorships = Child(work, "authorships");
	if (authorships.is_array()) {
		for (const json& authorship : authorships) {
			const json& person = Child(authorship, "author");
			Author author;
			author.name = Str(person, "display_name");
			if (author.name.empty())
				author.name = Str(authorship, "raw_author_name");
			if (author.name.empty())
				continue;
			author.orcid = StripOrcid(Str(person, "orcid"));
			const json& institutions = Child(authorship, "institutions");
			if (institutions.is_array()) {
				for (const json& institution : institutions) {
					std::string name = Str(institution, "display_name");
					if (!name.empty())
						author.affiliations.push_back(name);
				}
			}
			paper->authors.push_back(author);
		}
	}

	ParseKeywords(work, paper);

	// the abstract is delivered as an inverted index: word -> positions
	const json& inverted = Child(work, "abstract_inverted_index");
	if (inverted.is_object()) {
		std::vector<InvertedWord> index;
		for (auto it = inverted.begin(); it != inverted.end(); ++it) {
			InvertedWord entry;
			entry.first = it.key();
			if (it.value().is_array()) {
				for (const json& position : it.value()) {
					if (position.is_number_integer())
						entry.second.push_back(position.get<int>());
				}
			}
			index.push_back(entry);
		}
		paper->abstract = ReconstructAbstract(index);
	}

	// open access as OpenAlex knows it (Unpaywall's data is among it); Unpaywall overrides this when it is asked
	const json& access = Child(work, "open_access");
	paper->isOa = Bool(access, "is_oa");
	paper->oaStatus = Str(access, "oa_status");
	paper->locations.clear();
	const json& best = Child(work, "best_oa_location");
	if (best.is_object()) {
		Location location = OpenAlexLocation(best, true);
		if (!location.Url().empty())
			paper->locations.push_back(location);
	}
	const json& locations = Child(work, "locations");
	if (locations.is_array()) {
		for (const json& loc : locations) {
			if (!Bool(loc, "is_oa"))
				continue;
			Location location = OpenAlexLocation(loc, false);
			if (!location.Url().empty() && !HasUrl(paper->locations, location))
				paper->locations.push_back(location);
		}
	}

	paper->referencedWorks.clear();
	const json& references = Child(work, "referenced_works");
	if (references.is_array()) {
		for (const json& reference : references) {
			if (reference.is_string())
				paper->referencedWorks.push_back(ShortOpenAlexId(reference.get<std::string>()));
		}
	}
	return true;
}


bool
ParseUnpaywall(const std::string& body, Paper* paper)
{
	json record = json::parse(body, nullptr, false);
	if (record.is_discarded() || !record.is_object() || Child(record, "is_oa").is_null())
		return false;

	paper->fromUnpaywall = true;
	paper->isOa = Bool(record, "is_oa");
	paper->oaStatus = Str(record, "oa_status");

	std::string doi = NormalizeDoi(Str(record, "doi"));
	if (paper->doi.empty() && !doi.empty())
		paper->doi = doi;
	if (paper->title.empty())
		paper->title = Str(record, "title");
	if (paper->publisher.empty())
		paper->publisher = Str(record, "publisher");
	if (paper->venue.empty())
		paper->venue = Str(record, "journal_name");
	if (paper->date.empty())
		paper->date = Str(record, "published_date");

	paper->locations.clear();
	const json& best = Child(record, "best_oa_location");
	if (best.is_object()) {
		Location location = UnpaywallLocation(best, true);
		if (!location.Url().empty())
			paper->locations.push_back(location);
	}
	const json& others = Child(record, "oa_locations");
	if (others.is_array()) {
		for (const json& loc : others) {
			Location location = UnpaywallLocation(loc, false);
			if (!location.Url().empty() && !HasUrl(paper->locations, location))
				paper->locations.push_back(location);
		}
	}
	return true;
}


std::vector<Reference>
ParseOpenAlexList(const std::string& body)
{
	std::vector<Reference> references;
	json list = json::parse(body, nullptr, false);
	const json& results = Child(list, "results");
	if (!results.is_array())
		return references;

	for (const json& work : results) {
		Reference reference;
		reference.openAlexId = ShortOpenAlexId(Str(work, "id"));
		reference.doi = NormalizeDoi(Str(work, "doi"));
		reference.title = Str(work, "title");
		if (reference.title.empty())
			reference.title = Str(work, "display_name");
		reference.year = (int) Number(work, "publication_year", 0);
		if (!reference.openAlexId.empty())
			references.push_back(reference);
	}
	return references;
}

}	// namespace paperboy
