/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "InstitutionApi.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace institution {

namespace {

using nlohmann::json;

const char* const kRorApi = "https://api.ror.org/v2/organizations";
const char* const kOpenAlexApi = "https://api.openalex.org/institutions";
const size_t kMaxOtherNames = 12;

std::string
Text(const json& object, const char* key)
{
	if (!object.is_object())
		return std::string();
	auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}


bool
Has(const std::vector<std::string>& list, const std::string& value)
{
	return std::find(list.begin(), list.end(), value) != list.end();
}


void
AddUnique(std::vector<std::string>* list, const std::string& value)
{
	if (!value.empty() && !Has(*list, value))
		list->push_back(value);
}


bool
HasType(const json& name, const char* type)
{
	auto it = name.find("types");
	if (it == name.end() || !it->is_array())
		return false;
	for (const json& t : *it) {
		if (t.is_string() && t.get<std::string>() == type)
			return true;
	}
	return false;
}


/** the id of OpenAlex without the address: `https://openalex.org/I145847075` is `I145847075` */
std::string
OpenAlexBare(const std::string& iri)
{
	const size_t slash = iri.rfind('/');
	return slash == std::string::npos ? iri : iri.substr(slash + 1);
}


std::string
WithoutBlanks(const std::string& text)
{
	std::string result;
	for (char c : text) {
		if (c != ' ')
			result += c;
	}
	return result;
}

}	// namespace


std::string
UrlEncode(const std::string& text)
{
	static const char* const kHex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : text) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
			out += (char) c;
		} else {
			out += '%';
			out += kHex[c >> 4];
			out += kHex[c & 15];
		}
	}
	return out;
}


std::string
BareRorId(const std::string& text)
{
	std::string id = text;
	// (blanks around, the address with or without its scheme)
	while (!id.empty() && isspace((unsigned char) id.back()))
		id.pop_back();
	while (!id.empty() && isspace((unsigned char) id.front()))
		id.erase(id.begin());
	for (const char* prefix : {"https://ror.org/", "http://ror.org/", "ror.org/"}) {
		const std::string p = prefix;
		if (id.compare(0, p.size(), p) == 0) {
			id = id.substr(p.size());
			break;
		}
	}
	// a ROR id is 9 characters: 0, six of 0-9a-hj-km-np-tv-z, two digits (Crockford's base 32 without i, l, o, u; the last two are the checksum)
	if (id.size() != 9 || id[0] != '0')
		return std::string();
	for (char c : id) {
		if (!isalnum((unsigned char) c) || isupper((unsigned char) c))
			return std::string();
	}
	return id;
}


std::string
RorRecordUrl(const std::string& rorId)
{
	return std::string(kRorApi) + "/" + UrlEncode(rorId);
}


std::string
RorAffiliationUrl(const std::string& name)
{
	return std::string(kRorApi) + "?affiliation=" + UrlEncode(name);
}


std::string
OpenAlexInstitutionUrl(const std::string& rorId, const OpenAlexAccess& access)
{
	std::string url = std::string(kOpenAlexApi) + "?filter=ror:" + UrlEncode("https://ror.org/" + rorId);
	if (!access.mailto.empty())
		url += "&mailto=" + UrlEncode(access.mailto);
	if (!access.apiKey.empty())
		url += "&api_key=" + UrlEncode(access.apiKey);
	return url;
}


std::string
TypeText(const std::string& rorType)
{
	std::string text = rorType;
	if (!text.empty())
		text[0] = (char) toupper((unsigned char) text[0]);
	return text;
}


namespace {

bool
RecordOfJson(const json& organization, Record* record)
{
	if (!organization.is_object())
		return false;
	Record result;
	const std::string id = Text(organization, "id");
	result.rorId = BareRorId(id);
	if (result.rorId.empty())
		return false;
	result.status = Text(organization, "status");

	// the names: the one that the registry shows, the acronyms, then the aliases and the names in other languages
	std::vector<std::string> acronyms, aliases, labels;
	auto names = organization.find("names");
	if (names != organization.end() && names->is_array()) {
		for (const json& name : *names) {
			const std::string value = Text(name, "value");
			if (value.empty())
				continue;
			if (HasType(name, "ror_display"))
				result.name = value;
			else if (HasType(name, "acronym"))
				AddUnique(&acronyms, value);
			else if (HasType(name, "alias"))
				AddUnique(&aliases, value);
			else if (HasType(name, "label"))
				AddUnique(&labels, value);
		}
	}
	if (result.name.empty() && names != organization.end() && names->is_array() && !names->empty())
		result.name = Text(names->front(), "value");
	for (const auto* list : {&acronyms, &aliases, &labels}) {
		for (const std::string& value : *list) {
			if (value != result.name && result.otherNames.size() < kMaxOtherNames)
				AddUnique(&result.otherNames, value);
		}
	}

	auto types = organization.find("types");
	if (types != organization.end() && types->is_array()) {
		for (const json& type : *types) {
			if (type.is_string())
				AddUnique(&result.types, TypeText(type.get<std::string>()));
		}
	}

	AddUnique(&result.sameAs, "https://ror.org/" + result.rorId);
	auto links = organization.find("links");
	if (links != organization.end() && links->is_array()) {
		for (const json& link : *links) {
			const std::string type = Text(link, "type"), value = Text(link, "value");
			if (type == "website" && result.homepage.empty())
				result.homepage = value;
			else if (type == "wikipedia")
				AddUnique(&result.sameAs, value);
		}
	}

	auto externals = organization.find("external_ids");
	if (externals != organization.end() && externals->is_array()) {
		for (const json& external : *externals) {
			const std::string type = Text(external, "type");
			std::string value = Text(external, "preferred");
			if (value.empty()) {
				auto all = external.find("all");
				if (all != external.end() && all->is_array() && !all->empty() && all->front().is_string())
					value = all->front().get<std::string>();
			}
			if (value.empty())
				continue;
			if (type == "wikidata")
				AddUnique(&result.sameAs, "https://www.wikidata.org/entity/" + value);
			else if (type == "isni")
				AddUnique(&result.sameAs, "https://isni.org/isni/" + WithoutBlanks(value));
			else if (type == "grid")
				AddUnique(&result.sameAs, "https://www.grid.ac/institutes/" + value);
		}
	}

	auto locations = organization.find("locations");
	if (locations != organization.end() && locations->is_array() && !locations->empty()) {
		auto details = locations->front().find("geonames_details");
		if (details != locations->front().end()) {
			result.locality = Text(*details, "name");
			result.country = Text(*details, "country_code");
		}
	}

	auto relationships = organization.find("relationships");
	if (relationships != organization.end() && relationships->is_array()) {
		for (const json& relationship : *relationships) {
			Related related;
			related.rorId = BareRorId(Text(relationship, "id"));
			related.name = Text(relationship, "label");
			related.kind = Text(relationship, "type");
			if (!related.rorId.empty() && !related.kind.empty())
				result.related.push_back(related);
		}
	}

	*record = result;
	return true;
}

}	// namespace


bool
ParseRorRecord(const std::string& text, Record* record)
{
	const json parsed = json::parse(text, nullptr, false);
	return !parsed.is_discarded() && RecordOfJson(parsed, record);
}


std::vector<Hit>
ParseRorSearch(const std::string& text)
{
	std::vector<Hit> hits;
	const json parsed = json::parse(text, nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object())
		return hits;
	auto items = parsed.find("items");
	if (items == parsed.end() || !items->is_array())
		return hits;
	for (const json& item : *items) {
		Hit hit;
		// (the affiliation search wraps the organization with its score; the plain search has the organizations themselves)
		auto organization = item.find("organization");
		const bool wrapped = organization != item.end();
		if (!RecordOfJson(wrapped ? *organization : item, &hit.record))
			continue;
		if (wrapped) {
			auto score = item.find("score");
			if (score != item.end() && score->is_number())
				hit.score = score->get<double>();
			auto chosen = item.find("chosen");
			hit.chosen = chosen != item.end() && chosen->is_boolean() && chosen->get<bool>();
		}
		hits.push_back(hit);
	}
	std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
	return hits;
}


bool
MergeOpenAlex(const std::string& text, Record* record)
{
	const json parsed = json::parse(text, nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object())
		return false;
	auto results = parsed.find("results");
	if (results == parsed.end() || !results->is_array())
		return false;
	for (const json& institution : *results) {
		if (BareRorId(Text(institution, "ror")) != record->rorId)
			continue;

		const std::string id = Text(institution, "id");
		record->openAlexId = OpenAlexBare(id);
		AddUnique(&record->sameAs, id);

		auto works = institution.find("works_count");
		if (works != institution.end() && works->is_number_integer())
			record->worksCount = works->get<int64_t>();
		auto cited = institution.find("cited_by_count");
		if (cited != institution.end() && cited->is_number_integer())
			record->citedByCount = cited->get<int64_t>();

		if (record->types.empty() && !Text(institution, "type").empty())
			record->types.push_back(TypeText(Text(institution, "type")));
		if (record->country.empty())
			record->country = Text(institution, "country_code");
		if (record->homepage.empty())
			record->homepage = Text(institution, "homepage_url");

		auto ids = institution.find("ids");
		if (ids != institution.end() && ids->is_object()) {
			const std::string wikipedia = Text(*ids, "wikipedia"), wikidata = Text(*ids, "wikidata");
			AddUnique(&record->sameAs, wikipedia);
			if (!wikidata.empty()) {
				// (OpenAlex gives the address of the item, the registry the number: one link)
				const std::string number = OpenAlexBare(wikidata);
				AddUnique(&record->sameAs, "https://www.wikidata.org/entity/" + number);
			}
		}

		record->topics.clear();
		auto topics = institution.find("topics");
		if (topics != institution.end() && topics->is_array()) {
			for (const json& item : *topics) {
				Topic topic;
				topic.openAlexId = OpenAlexBare(Text(item, "id"));
				topic.name = Text(item, "display_name");
				auto count = item.find("count");
				if (count != item.end() && count->is_number_integer())
					topic.worksCount = count->get<int64_t>();
				if (!topic.name.empty())
					record->topics.push_back(topic);
			}
			std::stable_sort(record->topics.begin(), record->topics.end(), [](const Topic& a, const Topic& b) { return a.worksCount > b.worksCount; });
		}
		return true;
	}
	return false;
}


std::string
NormalizeName(const std::string& name)
{
	// UTF-8 letters of the languages of Europe become their plain letters (ä is a, ß is ss); other characters of a word stay as they are
	static const struct { const char* from; const char* to; } kMap[] = {
		{"\xC3\xA4", "a"}, {"\xC3\xB6", "o"}, {"\xC3\xBC", "u"}, {"\xC3\x9F", "ss"}, {"\xC3\x84", "a"}, {"\xC3\x96", "o"}, {"\xC3\x9C", "u"},
		{"\xC3\xA0", "a"}, {"\xC3\xA1", "a"}, {"\xC3\xA2", "a"}, {"\xC3\xA3", "a"}, {"\xC3\xA5", "a"}, {"\xC3\xA7", "c"},
		{"\xC3\xA8", "e"}, {"\xC3\xA9", "e"}, {"\xC3\xAA", "e"}, {"\xC3\xAB", "e"}, {"\xC3\xAC", "i"}, {"\xC3\xAD", "i"}, {"\xC3\xAE", "i"}, {"\xC3\xAF", "i"},
		{"\xC3\xB1", "n"}, {"\xC3\xB2", "o"}, {"\xC3\xB3", "o"}, {"\xC3\xB4", "o"}, {"\xC3\xB5", "o"}, {"\xC3\xB8", "o"},
		{"\xC3\xB9", "u"}, {"\xC3\xBA", "u"}, {"\xC3\xBB", "u"}, {"\xC3\xBD", "y"},
		{"\xC3\x80", "a"}, {"\xC3\x81", "a"}, {"\xC3\x89", "e"}, {"\xC3\x93", "o"}, {"\xC3\x9A", "u"}, {"\xC3\x87", "c"},
	};
	std::string out;
	bool blank = true;
	for (size_t i = 0; i < name.size();) {
		const unsigned char c = (unsigned char) name[i];
		std::string add;
		size_t used = 1;
		if (c >= 0x80) {
			bool mapped = false;
			for (const auto& entry : kMap) {
				const size_t length = std::char_traits<char>::length(entry.from);
				if (name.compare(i, length, entry.from) == 0) {
					add = entry.to;
					used = length;
					mapped = true;
					break;
				}
			}
			if (!mapped)
				add = name.substr(i, 1);
		} else if (isalnum(c)) {
			add = std::string(1, (char) tolower(c));
		}
		if (!add.empty()) {
			out += add;
			blank = false;
		} else if (!blank) {
			out += ' ';
			blank = true;
		}
		i += used;
	}
	if (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}


bool
NameMatches(const Record& record, const std::string& name)
{
	const std::string wanted = NormalizeName(name);
	if (wanted.empty())
		return false;
	if (NormalizeName(record.name) == wanted)
		return true;
	for (const std::string& other : record.otherNames) {
		if (NormalizeName(other) == wanted)
			return true;
	}
	return false;
}

}	// namespace institution
