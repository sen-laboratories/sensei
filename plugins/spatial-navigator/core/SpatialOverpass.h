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
};

/** The smallest side of the area that is asked for, in kilometres: a building or a location is a point, and what is around it is what people mean (as far as a place is near: 250 m on each side). */
const double kPoiMinKm = 0.5;
/** The largest side of the area that is asked for, in kilometres: a district has thousands of named things, a city hundreds of thousands, which no one can read. */
const double kPoiMaxKm = 3.0;

/** The area for the points of interest of a place: its box, at least kPoiMinKm on each side around its centre. Not valid if the place is larger than kPoiMaxKm on one side or has no box. */
Box PoiBox(const Box& place);

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

/** The page of the thing at OpenStreetMap: `https://www.openstreetmap.org/node/15080180`. */
std::string OsmUrl(const Poi& poi);

/** The key of an OpenStreetMap thing that a list of links names (`https://www.openstreetmap.org/way/212496461` is `way/212496461`); empty if there is none. */
std::string OsmKeyFrom(const std::string& links);

/** The key of the thing, `way/212496461` (as OsmKeyFrom gives it for the page). */
std::string OsmKey(const Poi& poi);

/** The distance in kilometres between two positions (haversine). */
double DistanceKm(double latitudeA, double longitudeA, double latitudeB, double longitudeB);

}	// namespace spatial
