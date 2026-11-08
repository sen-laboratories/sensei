/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "SpatialRelations.h"

/**
 * The points of interest in the area of a place, from OpenStreetMap through the Overpass API (https://overpass-api.de): everything with a name inside a box. No network and no Haiku here: the
 * answers are the JSON text, so everything can be tested on any computer.
 *
 * The data are by OpenStreetMap contributors and under the Open Database License (ODbL, https://www.openstreetmap.org/copyright). The public servers want an identifying User-Agent and
 * fair use (a few requests a minute: the answers are kept, see common/HttpCache.h).
 */
namespace spatial {

/** One named thing of OpenStreetMap: a node, a way or a relation (a building, a park) with its centre. */
struct Poi {
	std::string	osmType;			///< node, way or relation
	int64_t		osmId = 0;
	std::string	name;			///< in the language that was asked for, else the name that the thing has
	std::string	kind;			///< what it is, in a word or two for people: `supermarket`, `cafe`, `place of worship` (empty if the tags say nothing)
	std::string	kindUri;		///< the tag, as the page of the OpenStreetMap wiki: `https://wiki.openstreetmap.org/wiki/Tag:shop=supermarket`
	double		latitude = 0;
	double		longitude = 0;
	std::string	wikidataId;		///< `Q5943`
	std::string	website;
	std::string	address;		///< the street and the house number: `Hütteldorfer Straße 130B`
	std::string	postcode;
	std::string	locality;
	bool		notable = false;	///< has a Wikidata item or a Wikipedia article: known beyond the map (the sights come first)
	std::string	detail;			///< what more the tags say of what it is: the cuisine of a restaurant (`italian`), the speciality of a doctor (`gynaecology`), the denomination of a church, the operator of a stop
	std::string	description;	///< the description that a mapper wrote (in the language that was asked for, else the one that is there)
	std::vector<std::string> alternateNames;	///< the other names: alt_name, short_name, official_name, old_name, other than the name
};

/**
 * The label of the relation for the thing: what it is, which the name does not say (`supermarket`, `doctors (gynaecology)`, `restaurant (italian)`). Never the name itself: the name is the file
 * that the relation points at. Without a kind: what more is known (the detail, else the address, else `place`).
 */
std::string LabelFor(const Poi& poi);

/** The smallest side of the area that is asked for, in kilometres: a building or a location is a point, and what is around it is what people mean (twice the radius: 250 m on each side by default). */
const double kPoiMinKm = 0.5;
/** How many are returned by default, and the largest radius (in metres) that can be set. */
const int kPoiDefaultLimit = 40;
const int kPoiDefaultRadius = 250;
/** The largest side of the area that is asked for, in kilometres: a district has thousands of named things, a city hundreds of thousands, which no one can read. */
const double kPoiMaxKm = 3.0;

/** The area for the points of interest of a place: its box, at least kPoiMinKm on each side around its centre. Not valid if the place is larger than kPoiMaxKm on one side or has no box. */
Box PoiBox(const Box& place, double minKm = kPoiMinKm);

/**
 * The question for all named things in the box (`nwr[name](south,west,north,east); out center;`, the order of Overpass, not that of Nominatim), with a time limit of 25 seconds, to the
 * interpreter at @p endpoint (the public server, or a mirror: they answer the same). The same box gives the same text of the question on every server.
 */
std::string PoisUrl(const Box& box, const std::string& endpoint = "https://overpass-api.de/api/interpreter");

/** The address with the key in the place of `{key}` (percent-encoded); the address as it is if it has none. */
std::string WithKey(const std::string& endpoint, const std::string& key);

/**
 * The things of the answer, without those that are no points of interest: the roads (highway, except what is a stop or a stand), the borders (boundary), things that have a name and nothing
 * else. @p language is the language of the names (`de`; `name:de` if the thing has it). False if the text is not an answer of Overpass (a timeout is an answer with a remark and no elements).
 */
bool ParsePois(const std::string& json, const std::string& language, std::vector<Poi>* pois);

/**
 * The things in the order of interest, at most @p limit: by their distance from the middle, but a notable one (a Wikidata item or Wikipedia article: a church, a museum, a monument) counts
 * as three times nearer than a shop, so that the sights of a square are not pushed out by its shops. The nearest of two equally notable come first.
 */
std::vector<Poi> Rank(const std::vector<Poi>& pois, double latitude, double longitude, size_t limit);

/** The page of the thing at OpenStreetMap: `https://www.openstreetmap.org/node/15080180`. */
std::string OsmUrl(const Poi& poi);

/** The key of an OpenStreetMap thing that a list of links names (`https://www.openstreetmap.org/way/212496461` is `way/212496461`); empty if there is none. */
std::string OsmKeyFrom(const std::string& links);

/** The key of the thing, `way/212496461` (as OsmKeyFrom gives it for the page). */
std::string OsmKey(const Poi& poi);

/** The distance in kilometres between two positions (haversine). */
double DistanceKm(double latitudeA, double longitudeA, double latitudeB, double longitudeB);

}	// namespace spatial
