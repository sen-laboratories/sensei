/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

/**
 * The places that contain a place, from Wikidata ("located in the administrative territorial entity", P131, followed up to the country): one request to the query service answers the whole
 * chain with names, short descriptions and positions, so the parents of Stephansdom (Innere Stadt, Vienna, Austria) are known from its Wikidata link alone. No network and no Haiku here.
 *
 * Wikidata is in the public domain (CC0). The service wants an identifying User-Agent.
 */
namespace spatial {

/** Text for an address of a request: everything but letters, digits and `-._~` as %XX. */
std::string PercentEncode(const std::string& text);

/** A place that contains another one, as Wikidata knows it. */
struct Ancestor {
	std::string	id;				///< `Q1741`
	std::string	label;			///< the name, in the language that was asked for (else English)
	std::string	description;	///< the short description, `capital of and state in Austria`
	std::string	kind;			///< what kind of place it is, in a word: `country`, `city`, `district`... (empty if it is none of the well-known kinds)
	std::string	kindUri;		///< the Wikidata class of that kind (`http://www.wikidata.org/entity/Q515`), for `schema:additionalType`
	bool		hasPosition = false;
	double		latitude = 0;
	double		longitude = 0;
};

/**
 * The question for the places that contain the item (all of the chain, not only the next one), with the names in @p language and in English. It also asks which of the well-known kinds of
 * place each of them is (country, city, state, county, municipality, district, town, village, region, continent), by following the subclasses of the classes of the item: a place has many
 * classes at Wikidata and none of them is the one that people say ("federal state of Austria", "city with millions of inhabitants"), so the kinds are a short list in this order of
 * preference: Vienna is a city (and a state), Austria a country.
 */
std::string AncestorsUrl(const std::string& wikidataId, const std::string& language);

/** The places of the answer, at most @p limit; false if the text is not an answer of the query service. An empty chain is an answer. */
bool ParseAncestors(const std::string& json, std::vector<Ancestor>* ancestors, size_t limit = 16);

/** The Wikidata item (`Q5943`) that a list of links names (any separator); empty if there is none. */
std::string WikidataIdFrom(const std::string& links);

}	// namespace spatial
