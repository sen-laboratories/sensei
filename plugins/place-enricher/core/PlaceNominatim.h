/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * What the plugin needs of OpenStreetMap Nominatim (https://nominatim.org, the public service at nominatim.openstreetmap.org): the addresses to ask, and what the answers hold. No
 * network and no Haiku here: the answers are the JSON text, so everything can be tested on any computer.
 *
 * The data are by OpenStreetMap contributors and under the Open Database License (ODbL, https://www.openstreetmap.org/copyright). The public service wants an identifying User-Agent,
 * at most one request per second, that results are kept (see PlaceCache.h) and no autocomplete.
 */
namespace place {

/** One place as Nominatim describes it (an answer of search, lookup or reverse, format jsonv2). */
struct Hit {
	std::string	osmType;			///< node, way or relation
	int64_t		osmId = 0;
	std::string	name;				///< the name of the place (the local one, or the one in the language that was asked for)
	std::string	displayName;		///< the whole hierarchy in one line
	double		latitude = 0;
	double		longitude = 0;
	bool		hasBox = false;
	double		south = 0, north = 0, west = 0, east = 0;	///< the bounding box
	std::string	category;			///< the OpenStreetMap key: amenity, tourism, place, boundary, building...
	std::string	type;				///< its value: place_of_worship, city, administrative...
	std::string	addressType;		///< what the place is in an address: city, state, country, amenity...
	int			placeRank = 0;
	double		importance = 0;		///< 0 to 1
	std::string	wikidataId;			///< `Q5943`
	std::string	wikipedia;			///< `de:Stephansdom`
	std::string	website;
	std::vector<std::string> otherNames;	///< the names in other languages and the alternative names (not the name itself)
	std::string	road, houseNumber, postcode;
	std::string	locality;			///< the city, town or village
	std::string	region;				///< the state or province
	std::string	countryCode;		///< ISO 3166-1 alpha-2, upper case: `AT`
	std::string	country;
	std::string	wkt;				///< the boundary as Well-Known Text, only if it was asked for
};

/** How far the outline may differ from the real one, in degrees: about 11 metres. Enough for a building, and a city is 17 KB. */
const double kBoundaryTolerance = 0.0001;

/** A position (WGS 84, degrees): where a file says that it is. */
struct Position {
	double	latitude = 0;
	double	longitude = 0;
};

// ---- the addresses

/**
 * The search for a name or an address. @p language is the language of the names (a code like `de` or `de-AT`, empty: what the place has locally); @p boundary asks for the outline
 * (`polygon_text`, simplified: a boundary of 100 metres is accurate enough to tell which places are inside which). Up to @p limit answers.
 */
std::string SearchUrl(const std::string& query, const std::string& language, bool boundary, int limit = 8, double tolerance = kBoundaryTolerance);
/** One object of OpenStreetMap by its type (node, way, relation) and id: the place that was found before. */
std::string LookupUrl(const std::string& osmType, int64_t osmId, const std::string& language, bool boundary, double tolerance = kBoundaryTolerance);
/** The place at a position. */
std::string ReverseUrl(double latitude, double longitude, const std::string& language, bool boundary, double tolerance = kBoundaryTolerance);
/**
 * A coarser tolerance for a place whose outline is too large to keep (a country is 455 KB at the usual one): 0.5% of the size of its bounding box, from the usual one up to 0.01 degrees.
 */
double BoundaryTolerance(const Hit& hit);
/** Text for an address: everything but letters, digits and `-._~` as %XX. */
std::string PercentEncode(const std::string& text);

// ---- the answers
/** The hits of a search or lookup (a list), best first as Nominatim sorts them. False if the text is not an answer of Nominatim. An empty list is an answer. */
bool ParseHits(const std::string& json, std::vector<Hit>* hits);
/** The hit of a reverse lookup (one object). False if the text is not one, or says that there is nothing at the position. */
bool ParseHit(const std::string& json, Hit* hit);

// ---- what to make of them
/** The page of the object at OpenStreetMap: `https://www.openstreetmap.org/way/212496461`. */
std::string OsmUrl(const Hit& hit);
/** Where else the place is known: OpenStreetMap, Wikidata, Wikipedia (the links, for `schema:sameAs`). */
std::vector<std::string> SameAs(const Hit& hit);
/** The OpenStreetMap object that a list of links names (`https://www.openstreetmap.org/way/212496461`, any separator). False if there is none. */
bool OsmFrom(const std::string& links, std::string* osmType, int64_t* osmId);
/** The Wikidata item (`Q5943`) that a list of links names; empty if there is none. */
std::string WikidataFrom(const std::string& links);
/** Whether the place is an administrative area (a city, a state, a country: a boundary), whose shape is the place. */
bool IsAdministrative(const Hit& hit);
/**
 * What kind of place it is, as the IRI of the tag of OpenStreetMap (`https://wiki.openstreetmap.org/wiki/Tag:amenity=place_of_worship`). For an administrative boundary the tag is the
 * `place` that it is in an address (`Tag:place=city`), which says more than `boundary=administrative`. Empty if Nominatim says nothing.
 */
std::string PlaceTypeIri(const Hit& hit);
/** The postal address in one line (`Stephansplatz 3, 1010 Wien, Österreich`); empty for a place that has no street and no postcode (a city has no postal address). */
std::string AddressLine(const Hit& hit);
/** The names in other languages (from `namedetails`): each once, without the name of the place itself, at most @p limit. */
std::vector<std::string> OtherNames(const std::string& name, const std::vector<std::string>& candidates, size_t limit);

/**
 * Whether a hit is the place that was asked for: its name (or another name of it) is the one that was asked for, or holds it ("Stephans" for Stephansdom), or is held by it if what else is
 * asked for is where the place is ("Stephansdom Wien": Wien is in its address). "IMG 0001" is not an office called "IMG". Compared without case, blanks and marks. Always true if nothing
 * was asked for.
 */
bool TitleMatches(const Hit& hit, const std::string& wanted);
/** The distance in kilometres from the position to the place: 0 if the position is inside its bounding box, else to the nearest point of the box (to its centre if it has none). */
double DistanceKm(const Hit& hit, const Position& position);
/** The distance as text for a list: `at the position` (under 100 metres), `350 m`, `2.3 km`, `120 km`. */
std::string DistanceText(double kilometres);
/**
 * Whether the name of a file can be the name of a place, or is only a number that a camera gave (`IMG_0001.jpg`, `DSC00412`, `2023-05-01`): a name has more letters than digits (a blank,
 * a dash or a dot is neither). A file with a position and no title is looked up by the position when its name is none; it is not searched for.
 */
bool LooksLikeName(const std::string& name);
/**
 * ScoreHit for a file that has a position: the name (35%), how near the place is to the position (35%: within 1 km is 1, 10 km is 0.5, 100 km and more is 0), how well known it is (15%)
 * and the order of Nominatim (15%). A place that the position is inside of counts as near, which a city is for a photo that was taken in it.
 */
float ScoreHitNear(const Hit& hit, int position, int count, const std::string& wantedName, const Position& near);
/**
 * How good a hit is for the place that was asked for, from 0 to 1: the name (40%: the same is 1, one holds the other 0.6), how well known the place is (Nominatim's importance, 30%)
 * and the order of Nominatim (30%). The weights are a guess that is easy to change.
 */
float ScoreHit(const Hit& hit, int position, int count, const std::string& wantedName);

}	// namespace place
