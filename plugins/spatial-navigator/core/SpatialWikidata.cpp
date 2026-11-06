/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialWikidata.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include <nlohmann/json.hpp>

namespace spatial {

namespace {

using Json = nlohmann::json;

std::string
Encode(const std::string& text)
{
	std::string out;
	for (unsigned char c : text) {
		if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
			out += (char)c;
		} else {
			char buffer[4];
			std::snprintf(buffer, sizeof(buffer), "%%%02X", c);
			out += buffer;
		}
	}
	return out;
}

/** `de-AT` is `de` */
std::string
BaseLanguage(const std::string& language)
{
	const size_t dash = language.find_first_of("-_");
	std::string base = dash == std::string::npos ? language : language.substr(0, dash);
	return base.empty() ? "en" : base;
}

std::string
Value(const Json& binding, const char* name)
{
	auto it = binding.find(name);
	if (it == binding.end() || !it->is_object())
		return std::string();
	auto value = it->find("value");
	return value != it->end() && value->is_string() ? value->get<std::string>() : std::string();
}

}	// namespace


namespace {

/** the kinds of place that are told: the preference (1 first), the word and the class at Wikidata */
const struct {
	int			rank;
	const char*	name;
	const char*	cls;
} kKinds[] = {
	{1, "country", "Q6256"}, {1, "country", "Q3624078"},					// country, sovereign state
	{2, "city", "Q515"}, {2, "city", "Q1549591"},						// city, big city
	{3, "state", "Q107390"}, {3, "state", "Q10864048"},					// federated state, first-level administrative country subdivision
	{4, "county", "Q28575"},
	{5, "municipality", "Q15284"},
	{6, "district", "Q149621"},
	{7, "town", "Q3957"},
	{8, "village", "Q532"},
	{9, "region", "Q82794"}, {9, "continent", "Q5107"},
};

}	// namespace


std::string
AncestorsUrl(const std::string& wikidataId, const std::string& language)
{
	std::string languages = BaseLanguage(language);
	if (languages != "en")
		languages += ",en";
	std::string values;
	for (const auto& kind : kKinds)
		values += std::string("(wd:") + kind.cls + " " + std::to_string(kind.rank) + " \"" + kind.name + "\") ";
	// (MIN of the text of the preference, the word and the class: the preferred kind that fits)
	const std::string query = "SELECT ?item ?itemLabel ?itemDescription ?coord (MIN(CONCAT(STR(?rank), ?name, \"|\", STR(?root))) AS ?kind) WHERE { wd:" + wikidataId
		+ " wdt:P131+ ?item . OPTIONAL { ?item wdt:P625 ?coord } OPTIONAL { ?item wdt:P31/wdt:P279* ?root . VALUES (?root ?rank ?name) { " + values + "} } "
		"SERVICE wikibase:label { bd:serviceParam wikibase:language \"" + languages + "\" } } GROUP BY ?item ?itemLabel ?itemDescription ?coord LIMIT 40";
	return "https://query.wikidata.org/sparql?format=json&query=" + Encode(query);
}


bool
ParseAncestors(const std::string& text, std::vector<Ancestor>* ancestors, size_t limit)
{
	Json json = Json::parse(text, nullptr, false);
	if (!json.is_object())
		return false;
	auto results = json.find("results");
	if (results == json.end() || !results->is_object())
		return false;
	auto bindings = results->find("bindings");
	if (bindings == results->end() || !bindings->is_array())
		return false;

	ancestors->clear();
	for (const Json& binding : *bindings) {
		if (!binding.is_object() || ancestors->size() >= limit)
			continue;
		// the item is `http://www.wikidata.org/entity/Q40`
		const std::string uri = Value(binding, "item");
		const size_t slash = uri.rfind('/');
		const std::string id = slash == std::string::npos ? std::string() : uri.substr(slash + 1);
		if (id.size() < 2 || id[0] != 'Q')
			continue;
		bool known = false;
		for (const Ancestor& each : *ancestors)
			known = known || each.id == id;
		if (known)
			continue;		// (an item with more than one position comes once for each)

		Ancestor ancestor;
		ancestor.id = id;
		ancestor.label = Value(binding, "itemLabel");
		if (ancestor.label == id)
			ancestor.label.clear();		// (the service gives the id when it has no label)
		ancestor.description = Value(binding, "itemDescription");
		// the position is `Point(16.3725 48.208333333)`: longitude first
		const std::string coord = Value(binding, "coord");
		const size_t open = coord.find('(');
		if (coord.compare(0, 5, "Point") == 0 && open != std::string::npos) {
			char* end = nullptr;
			const double longitude = std::strtod(coord.c_str() + open + 1, &end);
			const double latitude = end != nullptr ? std::strtod(end, nullptr) : 0;
			ancestor.hasPosition = end != coord.c_str() + open + 1;
			ancestor.longitude = longitude;
			ancestor.latitude = latitude;
		}
		// the kind is `2city|http://www.wikidata.org/entity/Q515`: the preference, the word, the class
		const std::string kind = Value(binding, "kind");
		const size_t bar = kind.find('|');
		if (bar != std::string::npos && bar > 1) {
			ancestor.kind = kind.substr(1, bar - 1);
			ancestor.kindUri = kind.substr(bar + 1);
		}
		ancestors->push_back(ancestor);
	}
	return true;
}


std::string
WikidataIdFrom(const std::string& links)
{
	const std::string marker = "wikidata.org/wiki/Q";
	const size_t at = links.find(marker);
	if (at == std::string::npos)
		return std::string();
	size_t end = at + marker.size();
	while (end < links.size() && std::isdigit((unsigned char)links[end]))
		end++;
	if (end == at + marker.size())
		return std::string();
	return links.substr(at + marker.size() - 1, end - (at + marker.size() - 1));
}

}	// namespace spatial
