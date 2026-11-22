/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// Tests of core/: no Haiku and no GEOS needed, no network (the answer of Wikidata is recorded in tests/fixtures). Build and run with tests/run.sh.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "SpatialOverpass.h"
#include "SpatialRelations.h"
#include "SpatialSettings.h"
#include "SpatialWikidata.h"

#ifndef FIXTURES
#error "FIXTURES is the folder of the recorded answers (tests/run.sh sets it)"
#endif

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

static std::string
Fixture(const char* name)
{
	std::ifstream file(std::string(FIXTURES) + "/" + name, std::ios::binary);
	std::stringstream content;
	content << file.rdbuf();
	return content.str();
}


static void
TestRelations()
{
	using spatial::Relation;
	// the matrices that GEOS gave on Haiku (2026-11-06) for squares, a point, and the real outlines of Stephansdom and Wien
	CHECK(spatial::FromDe9im("2FFF1FFF2") == Relation::kEqual);
	CHECK(spatial::FromDe9im("FF2FF1212") == Relation::kDisjoint);
	CHECK(spatial::FromDe9im("FF2F11212") == Relation::kTouch);
	CHECK(spatial::FromDe9im("212101212") == Relation::kOverlap);
	CHECK(spatial::FromDe9im("2FF1FF212") == Relation::kWithin);			// (inside, the borders do not meet)
	CHECK(spatial::FromDe9im("2FF11F212") == Relation::kCoveredBy);			// (inside, touching the border)
	CHECK(spatial::FromDe9im("212FF1FF2") == Relation::kContains);
	CHECK(spatial::FromDe9im("212F11FF2") == Relation::kCovers);
	CHECK(spatial::FromDe9im("0FFFFF212") == Relation::kWithin);			// (a point inside an area)
	CHECK(spatial::FromDe9im("0FFFFFFF2") == Relation::kEqual);				// (the same point)
	CHECK(spatial::FromDe9im("FF0FFF0F2") == Relation::kDisjoint);			// (two different points)
	CHECK(spatial::FromDe9im("") == Relation::kNone);
	CHECK(spatial::FromDe9im("2FF1FF21") == Relation::kNone);				// (not nine characters)
	CHECK(spatial::FromDe9im("FFFFFFFFF") == Relation::kNone);

	// the opposite direction, the type and the label of each direction
	CHECK(spatial::Inverse(Relation::kWithin) == Relation::kContains && spatial::Inverse(Relation::kContains) == Relation::kWithin);
	CHECK(spatial::Inverse(Relation::kCoveredBy) == Relation::kCovers && spatial::Inverse(Relation::kCovers) == Relation::kCoveredBy);
	CHECK(spatial::Inverse(Relation::kTouch) == Relation::kTouch && spatial::Inverse(Relation::kEqual) == Relation::kEqual);
	CHECK(spatial::TypeOf(Relation::kWithin) == "relation/x-vnd.sen-labs.relation.spatial.within");
	CHECK(spatial::TypeOf(Relation::kContains) == spatial::TypeOf(Relation::kWithin));			// (one type, two labels)
	CHECK(spatial::TypeOf(Relation::kCovers) == "relation/x-vnd.sen-labs.relation.spatial.covered-by");
	CHECK(spatial::TypeOf(Relation::kCoveredBy) == spatial::TypeOf(Relation::kCovers));
	CHECK(spatial::TypeOf(Relation::kTouch) == "relation/x-vnd.sen-labs.relation.spatial.touch");
	CHECK(spatial::TypeOf(Relation::kOverlap) == "relation/x-vnd.sen-labs.relation.spatial.overlap");
	CHECK(spatial::TypeOf(Relation::kEqual) == "relation/x-vnd.sen-labs.relation.spatial.equal");
	CHECK(spatial::TypeOf(Relation::kDisjoint).empty() && spatial::TypeOf(Relation::kNone).empty());
	CHECK(spatial::LabelOf(Relation::kWithin) == "within" && spatial::LabelOf(Relation::kContains) == "contains");
	CHECK(spatial::LabelOf(Relation::kCoveredBy) == "covered by" && spatial::LabelOf(Relation::kCovers) == "covers");
	CHECK(spatial::LabelOf(Relation::kTouch) == "touches" && spatial::LabelOf(Relation::kOverlap) == "overlaps" && spatial::LabelOf(Relation::kEqual) == "equal to");
	CHECK(spatial::LabelOf(Relation::kDisjoint).empty());
	CHECK(spatial::CodeOf(Relation::kWithin) == "NTPP" && spatial::CodeOf(Relation::kContains) == "NTPPi" && spatial::CodeOf(Relation::kCoveredBy) == "TPP" && spatial::CodeOf(Relation::kCovers) == "TPPi");
	CHECK(spatial::CodeOf(Relation::kTouch) == "EC" && spatial::CodeOf(Relation::kOverlap) == "PO" && spatial::CodeOf(Relation::kEqual) == "EQ" && spatial::CodeOf(Relation::kDisjoint) == "DC");
	CHECK(spatial::CodeOf(Relation::kNone).empty());
	CHECK(spatial::KindOf(Relation::kWithin) == "non-tangential proper part" && spatial::KindOf(Relation::kContains) == "non-tangential proper part (inverse)");
	CHECK(spatial::KindOf(Relation::kCoveredBy) == "tangential proper part" && spatial::KindOf(Relation::kCovers) == "tangential proper part (inverse)");
	CHECK(spatial::KindOf(Relation::kTouch) == "externally connected" && spatial::KindOf(Relation::kOverlap) == "partially overlapping" && spatial::KindOf(Relation::kEqual) == "equal");
	CHECK(spatial::KindOf(Relation::kNone).empty());
	// what Wikidata says about the containment wins over a touch or overlap of simplified outlines
	CHECK(!spatial::AgreesWithWikidata(Relation::kOverlap, true) && !spatial::AgreesWithWikidata(Relation::kTouch, true));
	CHECK(spatial::AgreesWithWikidata(Relation::kWithin, true) && spatial::AgreesWithWikidata(Relation::kCoveredBy, true) && spatial::AgreesWithWikidata(Relation::kEqual, true));
	CHECK(spatial::AgreesWithWikidata(Relation::kOverlap, false) && spatial::AgreesWithWikidata(Relation::kTouch, false));

	// the boxes: only places whose boxes meet can be related
	spatial::Box vienna;
	vienna.south = 48.1179; vienna.north = 48.3227; vienna.west = 16.1818; vienna.east = 16.5775; vienna.valid = true;
	const spatial::Box cathedral = spatial::PointBox(48.2085, 16.3731);
	const spatial::Box salzburg = spatial::PointBox(47.8095, 13.0550);
	CHECK(spatial::BoxesMeet(vienna, cathedral));
	CHECK(spatial::BoxesMeet(cathedral, vienna));
	CHECK(!spatial::BoxesMeet(vienna, salzburg));
	spatial::Box neighbour;
	neighbour.south = 48.1179; neighbour.north = 48.3227; neighbour.west = 16.5775; neighbour.east = 16.9; neighbour.valid = true;
	CHECK(spatial::BoxesMeet(vienna, neighbour));							// (the edges meet: they can touch)
	CHECK(!spatial::BoxesMeet(vienna, spatial::Box()));						// (a box that is not valid meets nothing)
	CHECK(spatial::PointWkt(48.2085, 16.3731) == "POINT(16.3731 48.2085)");
	CHECK(spatial::PointWkt(-33.5, -70.25) == "POINT(-70.25 -33.5)");
}


static void
TestWikidata()
{
	const std::string url = spatial::AncestorsUrl("Q5943", "de-AT");
	CHECK(url.compare(0, 51, "https://query.wikidata.org/sparql?format=json&query") == 0);
	CHECK(url.find("%28wd%3AQ515%202%20%22city%22%29") != std::string::npos);				// (the kinds of place, in the order of preference)
	CHECK(url.find("wdt%3AP31%2Fwdt%3AP279%2A") != std::string::npos);
	CHECK(url.find("wd%3AQ5943%20wdt%3AP131%2B%20%3Fitem") != std::string::npos);			// (the whole chain: P131+)
	CHECK(url.find("wikibase%3Alanguage%20%22de%2Cen%22") != std::string::npos);
	CHECK(spatial::AncestorsUrl("Q1", "en").find("wikibase%3Alanguage%20%22en%22") != std::string::npos);

	std::vector<spatial::Ancestor> chain;
	CHECK(spatial::ParseAncestors(Fixture("ancestors-Q5943.json"), &chain));
	CHECK(chain.size() == 3);
	if (chain.size() == 3) {
		auto find = [&chain](const char* id) -> const spatial::Ancestor* {
			for (const spatial::Ancestor& each : chain) {
				if (each.id == id)
					return &each;
			}
			return nullptr;
		};
		const spatial::Ancestor* austria = find("Q40");
		const spatial::Ancestor* vienna = find("Q1741");
		const spatial::Ancestor* district = find("Q267329");
		CHECK(austria != nullptr && vienna != nullptr && district != nullptr);
		if (austria && vienna && district) {
			CHECK(austria->label == "Austria" && austria->description == "country in Central Europe");
			CHECK(vienna->label == "Vienna" && vienna->description == "capital of and state in Austria");
			CHECK(district->label == "Innere Stadt" && district->description == "first district of Vienna");
			CHECK(vienna->hasPosition && vienna->longitude > 16.37 && vienna->longitude < 16.38 && vienna->latitude > 48.20 && vienna->latitude < 48.21);	// (longitude comes first in Point(..))
			CHECK(austria->hasPosition && austria->longitude == 14.0 && austria->latitude == 48.0);
			// the kind of each: the word that people say, and its class
			CHECK(austria->kind == "country" && austria->kindUri == "http://www.wikidata.org/entity/Q3624078");
			CHECK(vienna->kind == "city" && vienna->kindUri == "http://www.wikidata.org/entity/Q515");	// (a city and a state: the city is preferred)
			CHECK(district->kind == "district" && district->kindUri == "http://www.wikidata.org/entity/Q149621");
		}
	}
	CHECK(spatial::ParseAncestors(Fixture("ancestors-Q5943.json"), &chain, 2) && chain.size() == 2);		// (a limit)

	CHECK(spatial::ParseAncestors("{\"results\":{\"bindings\":[]}}", &chain) && chain.empty());				// (an empty chain is an answer)
	CHECK(!spatial::ParseAncestors("not json", &chain));
	CHECK(!spatial::ParseAncestors("{\"error\":1}", &chain));
	// no label: the service says the id; one item that comes twice (two positions) is one ancestor
	CHECK(spatial::ParseAncestors("{\"results\":{\"bindings\":["
		"{\"item\":{\"value\":\"http://www.wikidata.org/entity/Q9\"},\"itemLabel\":{\"value\":\"Q9\"}},"
		"{\"item\":{\"value\":\"http://www.wikidata.org/entity/Q9\"},\"itemLabel\":{\"value\":\"Q9\"},\"coord\":{\"value\":\"Point(1 2)\"}}]}}", &chain));
	CHECK(chain.size() == 1 && chain[0].id == "Q9" && chain[0].label.empty() && !chain[0].hasPosition && chain[0].kind.empty() && chain[0].kindUri.empty());

	CHECK(spatial::WikidataIdFrom("https://www.openstreetmap.org/way/1,https://www.wikidata.org/wiki/Q5943,https://de.wikipedia.org/wiki/Stephansdom") == "Q5943");
	CHECK(spatial::WikidataIdFrom("https://www.openstreetmap.org/way/1").empty());
	CHECK(spatial::WikidataIdFrom("https://www.wikidata.org/wiki/Property:P18").empty());
}


static void
TestOverpass()
{
	using spatial::Box;
	// the area: a point and a small building are asked for 500 m around, a district for its box, a city not at all
	Box point = spatial::PointBox(48.2, 16.3);
	Box area = spatial::PoiBox(point);
	CHECK(area.valid && area.south < 48.2 && area.north > 48.2 && area.west < 16.3 && area.east > 16.3);
	CHECK(std::abs(spatial::DistanceKm(area.south, 16.3, area.north, 16.3) - spatial::kPoiMinKm) < 0.01);
	CHECK(std::abs(spatial::DistanceKm(48.2, area.west, 48.2, area.east) - spatial::kPoiMinKm) < 0.01);	// (the same in kilometres east to west: degrees of longitude are shorter)
	Box district;
	district.south = 48.19; district.north = 48.21; district.west = 16.29; district.east = 16.32; district.valid = true;
	Box asked = spatial::PoiBox(district);
	CHECK(asked.valid && std::abs(asked.south - district.south) < 1e-9 && std::abs(asked.north - district.north) < 1e-9 && std::abs(asked.west - district.west) < 1e-9);
	Box city;
	city.south = 48.12; city.north = 48.32; city.west = 16.18; city.east = 16.58; city.valid = true;
	CHECK(!spatial::PoiBox(city).valid);
	CHECK(!spatial::PoiBox(Box()).valid);

	// the question: the order of Overpass is south, west, north, east
	Box box;
	box.south = 48.196; box.north = 48.201; box.west = 16.298; box.east = 16.304; box.valid = true;
	const std::string url = spatial::PoisUrl(box);
	CHECK(url.rfind("https://overpass-api.de/api/interpreter?data=", 0) == 0);
	CHECK(url.find("%2848.196000%2C16.298000%2C48.201000%2C16.304000%29") != std::string::npos);
	CHECK(url.find("nwr%5Bname%5D") != std::string::npos && url.find("out%20center%3B") != std::string::npos && url.find("%5Bout%3Ajson%5D") != std::string::npos);

	// the answer (recorded for a box in the 14th district): the roads and the borders are no points of interest
	std::vector<spatial::Poi> pois;
	CHECK(spatial::ParsePois(Fixture("overpass-1140.json"), "de-AT", &pois));
	CHECK(!pois.empty());
	bool billa = false;
	for (const spatial::Poi& poi : pois) {
		CHECK(!poi.name.empty() && poi.latitude > 48 && poi.longitude > 16);
		CHECK(poi.name != "H\xC3\xBCtteldorfer Stra\xC3\x9F" "e");
		if (poi.osmType == "node" && poi.osmId == 15080180) {
			billa = true;
			CHECK(poi.name == "Billa Plus" && poi.kind == "supermarket" && poi.kindUri == "https://wiki.openstreetmap.org/wiki/Tag:shop=supermarket");
			CHECK(poi.address == "H\xC3\xBCtteldorfer Stra\xC3\x9F" "e 130B" && poi.postcode == "1140" && poi.locality == "Wien" && poi.website == "https://www.billa.at");
			CHECK(spatial::OsmUrl(poi) == "https://www.openstreetmap.org/node/15080180" && spatial::OsmKey(poi) == "node/15080180");
		}
	}
	CHECK(billa);

	// a way has its centre; the name in the language wins; a road, a border, a bare name and a thing without a position are left out
	const std::string text = "{\"elements\":["
		"{\"type\":\"way\",\"id\":7,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"Church\",\"name:de\":\"Kirche\",\"amenity\":\"place_of_worship\",\"wikidata\":\"Q42\"}},"
		"{\"type\":\"way\",\"id\":8,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"Main Street\",\"highway\":\"residential\"}},"
		"{\"type\":\"node\",\"id\":9,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Stop\",\"highway\":\"bus_stop\",\"public_transport\":\"platform\"}},"
		"{\"type\":\"relation\",\"id\":10,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"District\",\"boundary\":\"administrative\"}},"
		"{\"type\":\"node\",\"id\":11,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Label\"}},"
		"{\"type\":\"way\",\"id\":12,\"tags\":{\"name\":\"Nowhere\",\"amenity\":\"cafe\"}},"
		"{\"type\":\"way\",\"id\":13,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"Haus\",\"building\":\"yes\"}},"
		"{\"type\":\"node\",\"id\":7,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Same number, other type\",\"shop\":\"bakery\"}},"
		"{\"type\":\"node\",\"id\":14,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Stop position\",\"public_transport\":\"stop_position\",\"bus\":\"yes\"}},"
		"{\"type\":\"node\",\"id\":16,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Tram position\",\"public_transport\":\"stop_position\",\"railway\":\"tram_stop\"}},"
		"{\"type\":\"way\",\"id\":17,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"Tram platform\",\"public_transport\":\"platform\",\"railway\":\"platform\"}},"
		"{\"type\":\"node\",\"id\":18,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Tram stop\",\"public_transport\":\"platform\",\"railway\":\"tram_stop\"}},"
		"{\"type\":\"way\",\"id\":15,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"Platform\",\"public_transport\":\"platform\"}}]}";
	CHECK(spatial::ParsePois(text, "de", &pois));
	CHECK(pois.size() == 5);
	if (pois.size() == 5) {
		CHECK(pois[0].osmType == "way" && pois[0].name == "Kirche" && pois[0].kind == "place of worship" && pois[0].wikidataId == "Q42" && pois[0].latitude == 48.2);
		CHECK(pois[1].name == "Stop" && pois[1].kind == "bus stop");		// (the stop itself, not its position or platform)
		CHECK(pois[2].name == "Haus" && pois[2].kind == "building" && pois[2].kindUri == "https://wiki.openstreetmap.org/wiki/Tag:building=yes");
		CHECK(pois[3].osmType == "node" && pois[3].osmId == 7 && pois[3].kind == "bakery");
		CHECK(pois[4].name == "Tram stop" && pois[4].kind == "tram stop");
	}
	CHECK(spatial::ParsePois(text, "en", &pois) && pois[0].name == "Church");

	// what the label says: what it is, never the name; the tags that add to it
	{
		const std::string tagged = "{\"elements\":["
			"{\"type\":\"node\",\"id\":21,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Da Luigi\",\"amenity\":\"restaurant\",\"cuisine\":\"italian;pizza\",\"description\":\"Since 1950\"}},"
			"{\"type\":\"node\",\"id\":22,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Dr. Weiss\",\"amenity\":\"doctors\",\"healthcare:speciality\":\"gynaecology\"}},"
			"{\"type\":\"node\",\"id\":23,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Karlskirche\",\"amenity\":\"place_of_worship\",\"religion\":\"christian\",\"denomination\":\"roman_catholic\",\"alt_name\":\"St. Charles;Karls\",\"short_name\":\"Karls\",\"official_name\":\"Karlskirche\"}},"
			"{\"type\":\"node\",\"id\":24,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Leyserstra\\u00dfe\",\"railway\":\"tram_stop\",\"operator\":\"Wiener Linien\"}},"
			"{\"type\":\"node\",\"id\":25,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Haus Eins\",\"addr:street\":\"Gasse\",\"addr:housenumber\":\"1\"}}]}";
		std::vector<spatial::Poi> labelled;
		CHECK(spatial::ParsePois(tagged, "de", &labelled) && labelled.size() == 5);
		if (labelled.size() == 5) {
			CHECK(spatial::LabelFor(labelled[0]) == "restaurant (italian, pizza)" && labelled[0].description == "Since 1950");
			CHECK(spatial::LabelFor(labelled[1]) == "doctors (gynaecology)");
			CHECK(spatial::LabelFor(labelled[2]) == "place of worship (roman catholic)");
			CHECK(labelled[2].alternateNames.size() == 2 && labelled[2].alternateNames[0] == "St. Charles" && labelled[2].alternateNames[1] == "Karls");		// (not the name itself, each once)
			CHECK(spatial::LabelFor(labelled[3]) == "tram stop (Wiener Linien)");
			CHECK(spatial::LabelFor(labelled[4]) == "Gasse 1");			// (no kind: what more is known, never the name)
			for (const spatial::Poi& poi : labelled)
				CHECK(spatial::LabelFor(poi) != poi.name);
		}
	}

	// what is gone or not there yet is no point of interest (the old track of a tram line), what stands next to it stays
	{
		const std::string gone = "{\"elements\":["
			"{\"type\":\"way\",\"id\":31,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"Old track of tram 60\",\"railway\":\"abandoned\"}},"
			"{\"type\":\"way\",\"id\":32,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"Old line\",\"abandoned:railway\":\"tram\"}},"
			"{\"type\":\"node\",\"id\":33,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Closed bakery\",\"disused:shop\":\"bakery\"}},"
			"{\"type\":\"node\",\"id\":34,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Old cafe\",\"amenity\":\"cafe\",\"disused\":\"yes\"}},"
			"{\"type\":\"way\",\"id\":35,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"New tower\",\"building\":\"construction\"}},"
			"{\"type\":\"way\",\"id\":36,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"Haus Alt\",\"building\":\"yes\",\"disused:shop\":\"bakery\"}},"
			"{\"type\":\"node\",\"id\":37,\"lat\":48.2,\"lon\":16.3,\"tags\":{\"name\":\"Ruine\",\"historic\":\"ruins\"}},"
			"{\"type\":\"way\",\"id\":38,\"center\":{\"lat\":48.2,\"lon\":16.3},\"tags\":{\"name\":\"Tram 60\",\"railway\":\"tram_stop\"}}]}";
		std::vector<spatial::Poi> alive;
		CHECK(spatial::ParsePois(gone, "de", &alive) && alive.size() == 3);
		if (alive.size() == 3)
			CHECK(alive[0].name == "Haus Alt" && alive[1].name == "Ruine" && alive[2].name == "Tram 60");
	}

	CHECK(spatial::ParsePois("{\"elements\":[]}", "de", &pois) && pois.empty());			// (nothing there is an answer)
	CHECK(!spatial::ParsePois("{\"remark\":\"runtime error: Query timed out\"}", "de", &pois));
	CHECK(!spatial::ParsePois("not json", "de", &pois));

	CHECK(spatial::OsmKeyFrom("https://www.wikidata.org/wiki/Q1,https://www.openstreetmap.org/way/212496461") == "way/212496461");
	CHECK(spatial::OsmKeyFrom("https://www.openstreetmap.org/relation/9, https://www.openstreetmap.org/node/1") == "relation/9");
	CHECK(spatial::OsmKeyFrom("https://www.openstreetmap.org/user/7").empty() && spatial::OsmKeyFrom("https://www.openstreetmap.org/node/").empty());
	CHECK(std::abs(spatial::DistanceKm(48.2085, 16.3731, 48.2085, 16.3731)) < 1e-9);
	// the order of interest: a sight counts as three times nearer than a shop; at most the limit
	{
		auto make = [](const char* name, double latitude, bool notable) {
			spatial::Poi poi;
			poi.name = name; poi.latitude = latitude; poi.longitude = 16.3; poi.notable = notable;
			return poi;
		};
		std::vector<spatial::Poi> all = {make("shop-near", 48.2001, false), make("church-farther", 48.2020, true), make("cafe", 48.2008, false), make("museum-far", 48.2100, true)};
		std::vector<spatial::Poi> ranked = spatial::Rank(all, 48.2, 16.3, 3);
		CHECK(ranked.size() == 3 && ranked[0].name == "shop-near" && ranked[1].name == "church-farther" && ranked[2].name == "cafe");		// (the church, 222 m, counts as 74 m: before the café at 89 m)
		CHECK(spatial::Rank(all, 48.2, 16.3, 10).size() == 4 && spatial::Rank(all, 48.2, 16.3, 10)[3].name == "museum-far");
		CHECK(spatial::Rank(all, 48.2, 16.3, 0).empty());
	}
	// the area follows the radius: 1 km across for a radius of 500 m
	{
		spatial::Box pointBox = spatial::PointBox(48.2, 16.3);
		spatial::Box wide = spatial::PoiBox(pointBox, 1.0);
		CHECK(wide.valid && std::abs(spatial::DistanceKm(wide.south, 16.3, wide.north, 16.3) - 1.0) < 0.01);
	}
	CHECK(std::abs(spatial::DistanceKm(48.2085, 16.3731, 47.0707, 15.4395) - 144.6) < 0.5);		// (Vienna - Graz)
}


static void
TestSettings()
{
	using namespace spatial;
	// with no setting the list is the public server, then the mirrors that need no key, in the order of the list; the servers that need a key and the custom one are off
	Settings settings;
	{
		const std::vector<Server> servers = ActiveServers(settings);
		CHECK(servers.size() == 3 && servers[0].id == "default" && servers[0].isPublic && servers[0].endpoint == kDefaultOverpassUrl);
		CHECK(servers[1].id == "private-coffee" && servers[1].endpoint == "https://overpass.private.coffee/api/interpreter");
		CHECK(servers[2].id == "vk-maps" && servers[2].endpoint == "https://maps.mail.ru/osm/tools/overpass/api/interpreter" && servers[2].key.empty());
		CHECK(ServerOrder(settings).size() == Mirrors().size() && !ServerIsOn(settings, "nextgis") && !ServerIsOn(settings, "custom") && ServerIsOn(settings, "vk-maps"));
	}
	// the setting names the order and switches off with a -; what it does not name follows, as the list has it; a name that is none of the list is left out
	{
		Settings listed;
		ParseSettings("# a comment\noverpass_servers = vk-maps -default bogus private-coffee\n", &listed);
		const std::vector<std::string> order = ServerOrder(listed);
		CHECK(order.size() == Mirrors().size() && order[0] == "vk-maps" && order[1] == "default" && order[2] == "private-coffee" && order[3] == "nextgis");
		CHECK(ServerIsOn(listed, "vk-maps") && !ServerIsOn(listed, "default") && ServerIsOn(listed, "private-coffee"));
		const std::vector<Server> servers = ActiveServers(listed);
		CHECK(servers.size() == 2 && servers[0].id == "vk-maps" && servers[1].id == "private-coffee");
		std::vector<std::string> on;
		for (const std::string& id : order) {
			if (ServerIsOn(listed, id))
				on.push_back(id);
		}
		CHECK(ServersText(order, on) == "vk-maps -default private-coffee -nextgis -overpass-ch -custom" || ServersText(order, on).find("vk-maps -default private-coffee") == 0);
		Settings again;
		ParseSettings("overpass_servers = " + ServersText(order, on), &again);
		CHECK(ServerOrder(again) == order && ServerIsOn(again, "custom") == ServerIsOn(listed, "custom"));
		// nothing is on: the public server (the user is not left with no way to ask)
		Settings none;
		ParseSettings("overpass_servers = -default -private-coffee -vk-maps", &none);
		CHECK(ActiveServers(none).size() == 1 && ActiveServers(none)[0].id == "default");
	}
	// a file from before the list: the mirror that was chosen is the only one that is on, with its key
	{
		Settings old;
		ParseSettings("overpass_mirror = vk-maps\n", &old);
		CHECK(ActiveServers(old).size() == 1 && ActiveServers(old)[0].id == "vk-maps");
		Settings oldKeyed;
		ParseSettings("overpass_mirror = overpass-ch\noverpass_key = secret123\n", &oldKeyed);
		CHECK(ActiveServers(oldKeyed).size() == 1 && ActiveServers(oldKeyed)[0].id == "overpass-ch" && ActiveServers(oldKeyed)[0].key == "secret123");
		// (the list in the file wins, wherever the old line is)
		Settings both;
		ParseSettings("overpass_servers = default\noverpass_mirror = vk-maps\n", &both);
		CHECK(ActiveServers(both)[0].id == "default" && ServerOrder(both)[0] == "default");
	}
	// a server with a key: it is asked only with its key, and each has its own
	{
		Settings keyed;
		ParseSettings("overpass_servers = nextgis overpass-ch default -private-coffee -vk-maps -custom\n", &keyed);
		CHECK(ServerIsOn(keyed, "nextgis") && ActiveServers(keyed).size() == 1 && ActiveServers(keyed)[0].id == "default");		// (no key: left out)
		ParseSettings("overpass_key_overpass_ch = abc\n", &keyed);
		CHECK(KeySettingName("overpass-ch") == "overpass_key_overpass_ch" && KeyOf(keyed, "overpass-ch") == "abc" && KeyOf(keyed, "nextgis").empty());
		std::vector<Server> servers = ActiveServers(keyed);
		CHECK(servers.size() == 2 && servers[0].id == "overpass-ch" && servers[0].key == "abc" && servers[0].endpoint == "https://api.overpass.ch/v1/{key}/interpreter");
		CHECK(WithKey(servers[0].endpoint, servers[0].key) == "https://api.overpass.ch/v1/abc/interpreter");
		ParseSettings("overpass_key_nextgis = xyz\n", &keyed);
		servers = ActiveServers(keyed);
		CHECK(servers.size() == 3 && servers[0].id == "nextgis" && servers[0].key == "xyz" && servers[1].id == "overpass-ch" && servers[1].key == "abc");
		ParseSettings("overpass_key_overpass_ch =\n", &keyed);
		CHECK(KeyOf(keyed, "overpass-ch").empty() && ActiveServers(keyed).size() == 2);
		CHECK(ValidateSetting("overpass_key_nextgis", "abc") && !ValidateSetting("overpass_key_nextgis", "a b") && ValidateSetting("overpass_key_overpass_ch", ""));
		CHECK(!ValidateSetting("overpass_key", "abc"));		// (a key is the key of a server: it has the name of one)
	}
	// the custom mirror: with an address (and a key if the flag says so), as a row of the list
	{
		Settings custom;
		ParseSettings("overpass_servers = custom default -private-coffee -vk-maps\noverpass_url = https://overpass.example.org/api/interpreter\n", &custom);
		CHECK(ActiveServers(custom).size() == 2 && ActiveServers(custom)[0].id == "custom" && ActiveServers(custom)[0].endpoint == "https://overpass.example.org/api/interpreter");
		ParseSettings("overpass_url = https://overpass.geofabrik.de/{key}/api/interpreter", &custom);
		CHECK(ActiveServers(custom).size() == 1);				// (a place for the key and no key)
		ParseSettings("overpass_key_custom = g3o", &custom);
		CHECK(ActiveServers(custom).size() == 2 && WithKey(ActiveServers(custom)[0].endpoint, ActiveServers(custom)[0].key) == "https://overpass.geofabrik.de/g3o/api/interpreter");
		ParseSettings("overpass_url =\n", &custom);
		CHECK(ActiveServers(custom).size() == 1 && ActiveServers(custom)[0].id == "default");		// (no address: left out)
		ParseSettings("overpass_url = https://overpass.example.org/api/interpreter\noverpass_custom_key = yes", &custom);
		CHECK(ActiveServers(custom)[0].key == "g3o");
		ParseSettings("overpass_custom_key = no", &custom);
		CHECK(ActiveServers(custom)[0].key.empty());
		CHECK(ValidateSetting("overpass_custom_key", "yes") && !ValidateSetting("overpass_custom_key", "maybe"));
	}
	// when some are on and none is complete there is none, and the user is told: the public server is not asked behind the back of the settings
	{
		Settings incomplete;
		ParseSettings("overpass_servers = -default -private-coffee -vk-maps nextgis", &incomplete);
		CHECK(ActiveServers(incomplete).empty());
		CHECK(std::string(kMirrorNotWorking) == "Your configured OSM mirror is not working, please check settings.");
	}
	// a server that the test found not reachable is skipped (not the public one: it is often only busy), until a test of it works
	{
		Settings failed;
		ParseSettings("overpass_servers = private-coffee default -vk-maps\noverpass_failed = private-coffee", &failed);
		CHECK(ActiveServers(failed).size() == 1 && ActiveServers(failed)[0].id == "default");
		Server tested;
		CHECK(MakeServer(failed, "private-coffee", &tested) && tested.endpoint == "https://overpass.private.coffee/api/interpreter");		// (the test asks it all the same)
		ParseSettings("overpass_failed = default vk-maps bogus", &failed);
		CHECK(failed.failedIds.size() == 1 && failed.failedIds[0] == "vk-maps" && ActiveServers(failed).size() == 2);
		ParseSettings("overpass_failed =", &failed);
		CHECK(failed.failedIds.empty());
	}
	// the address and the flag of every server can be changed by the user; what is not changed is what the list says
	{
		Settings own;
		CHECK(AddressOf(own, "private-coffee") == DefaultAddress("private-coffee") && AddressOf(own, "custom").empty());
		CHECK(AddressSettingName("private-coffee") == "overpass_url_private_coffee" && AddressSettingName("custom") == "overpass_url" && AddressSettingName("nope").empty());
		CHECK(NeedsKeySettingName("overpass-ch") == "overpass_needs_key_overpass_ch" && NeedsKeySettingName("custom") == "overpass_custom_key");
		ParseSettings("overpass_url_private_coffee = https://mirror.example.org/api/interpreter\noverpass_needs_key_vk_maps = yes\noverpass_needs_key_nextgis = no\n", &own);
		CHECK(AddressOf(own, "private-coffee") == "https://mirror.example.org/api/interpreter" && AddressOf(own, "vk-maps") == DefaultAddress("vk-maps"));
		CHECK(NeedsKeyFlag(own, "vk-maps") && !NeedsKeyFlag(own, "nextgis") && DefaultNeedsKeyFlag("nextgis") && !DefaultNeedsKeyFlag("vk-maps"));
		// (NextGIS has the place for the key in its address: it needs one whatever the flag says)
		CHECK(ServerNeedsKey(own, "nextgis") && ServerNeedsKey(own, "vk-maps") && !ServerNeedsKey(own, "private-coffee"));
		Server made;
		std::string problem;
		CHECK(MakeServer(own, "private-coffee", &made) && made.endpoint == "https://mirror.example.org/api/interpreter");
		CHECK(!MakeServer(own, "vk-maps", &made, &problem) && problem == "Needs an API key");
		ParseSettings("overpass_key_vk_maps = secret\n", &own);
		CHECK(MakeServer(own, "vk-maps", &made) && made.key == "secret");
		// the address of the list again: an empty value, or a bad one that is left out
		ParseSettings("overpass_url_private_coffee =\n", &own);
		CHECK(AddressOf(own, "private-coffee") == DefaultAddress("private-coffee"));
		ParseSettings("overpass_url_private_coffee = ftp://x\n", &own);
		CHECK(AddressOf(own, "private-coffee") == DefaultAddress("private-coffee"));
		CHECK(ValidateSetting("overpass_url_private_coffee", "") && ValidateSetting("overpass_url_private_coffee", "https://a.example.org/x") && !ValidateSetting("overpass_url_private_coffee", "a.example.org"));
		CHECK(ValidateSetting("overpass_needs_key_nextgis", "yes") && ValidateSetting("overpass_needs_key_nextgis", "") && !ValidateSetting("overpass_needs_key_nextgis", "maybe"));
	}
	// what a server needs: a key (the ones made for it, the custom mirror with the flag or a place for the key in its address), an address (the custom mirror)
	{
		Settings needs;
		CHECK(!ServerNeedsKey(needs, "default") && ServerNeedsKey(needs, "nextgis") && ServerNeedsKey(needs, "overpass-ch") && !ServerNeedsKey(needs, "custom"));
		Server made;
		std::string problem;
		CHECK(!MakeServer(needs, "nextgis", &made, &problem) && problem == "Needs an API key");
		CHECK(!MakeServer(needs, "custom", &made, &problem) && problem == "Needs an address");
		ParseSettings("overpass_key_nextgis = k\noverpass_url = https://x.example.org/{key}/api/interpreter", &needs);
		CHECK(MakeServer(needs, "nextgis", &made, &problem) && made.key == "k" && ServerNeedsKey(needs, "custom") && !MakeServer(needs, "custom", &made, &problem) && problem == "Needs an API key");
		CHECK(MakeServer(needs, "default", &made) && made.isPublic && made.key.empty());
	}
	// the numbers: radius and the most points of interest, in their ranges
	Settings numbers;
	CHECK(numbers.poiRadius == 250 && numbers.poiLimit == 40);
	ParseSettings("poi_radius = 600\npoi_limit = 25", &numbers);
	CHECK(numbers.poiRadius == 600 && numbers.poiLimit == 25);
	ParseSettings("poi_radius = 5\npoi_limit = lots", &numbers);
	CHECK(numbers.poiRadius == 600 && numbers.poiLimit == 25);		// (out of range, not a number: what was there stays)
	CHECK(ValidateSetting("poi_radius", "1500") && !ValidateSetting("poi_radius", "1501") && !ValidateSetting("poi_limit", "4") && ValidateSetting("poi_limit", "200"));
	// how long the answers are kept: a day, 1 to 168 hours
	CHECK(numbers.poiCacheHours == 24);
	ParseSettings("poi_cache_hours = 48", &numbers);
	CHECK(numbers.poiCacheHours == 48);
	ParseSettings("poi_cache_hours = 0", &numbers);
	ParseSettings("poi_cache_hours = soon", &numbers);
	CHECK(numbers.poiCacheHours == 48);		// (out of range, not a number: what was there stays)
	CHECK(ValidateSetting("poi_cache_hours", "1") && ValidateSetting("poi_cache_hours", "168") && !ValidateSetting("poi_cache_hours", "169") && !ValidateSetting("poi_cache_hours", "0"));
	// the default style of the map: one of the styles of the list
	CHECK(numbers.mapStyle == "topo");
	ParseSettings("map_style = hot", &numbers);
	CHECK(numbers.mapStyle == "hot");
	ParseSettings("map_style = voyager", &numbers);
	CHECK(numbers.mapStyle == "hot");		// (a style that is not offered: what was there stays)
	CHECK(ValidateSetting("map_style", "cyclosm") && !ValidateSetting("map_style", "voyager"));
	// what is not valid is ignored: an address that is none
	Settings address;
	ParseSettings("overpass_url = https://overpass.example.org/api/interpreter\n", &address);
	ParseSettings("overpass_url = ftp://x\n", &address);
	CHECK(address.overpassUrl == "https://overpass.example.org/api/interpreter");
	const std::vector<Mirror>& mirrors = Mirrors();
	CHECK(mirrors.size() == 6 && std::string(mirrors.back().id) == "custom");		// (the last one is the custom mirror)
	for (const Mirror& mirror : mirrors)
		CHECK(std::string(mirror.name).size() > 3 && std::string(mirror.info).size() > 10);		// (the list shows a name and a description)

	// what the test of a server says: the status as the text, the state for the icon, the detail in a few lines
	CHECK(HttpStatusText(200) == "200 OK" && HttpStatusText(503) == "503 Service Unavailable" && HttpStatusText(418) == "HTTP 418");
	const ServerTest works = JudgeServerTest("geo.example.org", 200, false, "", true, 12, 0.43, 20, "");
	CHECK(works.text == "200 OK" && works.state == "ok" && works.detail == "geo.example.org\nAnswered in 0.4 s (12 places in the test area).");
	const ServerTest notOverpass = JudgeServerTest("example.org", 200, false, "", false, 0, 0.2, 20, "");
	CHECK(notOverpass.state == "warning" && notOverpass.text == "200 OK, not Overpass");
	const ServerTest refused = JudgeServerTest("h", 403, false, "", false, 0, 1.0, 20, "A remark.");
	CHECK(refused.text == "403 Forbidden" && refused.state == "warning" && refused.detail == "h\nStatus 403 Forbidden after 1.0 s.\nThe key is missing, wrong or not for this server.\nA remark.");
	CHECK(JudgeServerTest("h", 400, false, "", false, 0, 1.0, 20, "").state == "warning" && JudgeServerTest("h", 429, false, "", false, 0, 1.0, 20, "").state == "warning");
	const ServerTest down = JudgeServerTest("h", 502, false, "", false, 0, 0.1, 20, "");
	CHECK(down.text == "502 Bad Gateway" && down.state == "error" && down.detail.find("overloaded or down") != std::string::npos);
	const ServerTest silent = JudgeServerTest("h", 0, true, "", false, 0, 20.0, 20, "");
	CHECK(silent.text == "No answer" && silent.state == "error" && silent.detail == "h\nNo answer in 20 s.\nThe server is down, or the address is wrong.");
	const ServerTest gone = JudgeServerTest("h", 0, false, "Host not found", false, 0, 0.1, 20, "");
	CHECK(gone.text == "Not reachable" && gone.state == "error" && gone.detail.find("Host not found") != std::string::npos);

	std::string why;
	CHECK(ValidateSetting("overpass_servers", "custom -default") && ValidateSetting("overpass_servers", ""));
	CHECK(!ValidateSetting("overpass_servers", "default other", &why) && !why.empty());
	CHECK(ValidateSetting("overpass_url", "") && ValidateSetting("overpass_url", "http://localhost:8080/api/interpreter"));
	CHECK(!ValidateSetting("overpass_url", "overpass.example.org") && !ValidateSetting("overpass_url", "https://a b"));
	CHECK(!ValidateSetting("overpass_url", "https://example.org/#x"));
	CHECK(!ValidateSetting("nothing", "x"));

	// the question goes to the interpreter that is chosen; the key can be in the address
	Box box;
	box.south = 48.196; box.north = 48.201; box.west = 16.298; box.east = 16.304; box.valid = true;
	const std::string vk = PoisUrl(box, "https://maps.mail.ru/osm/tools/overpass/api/interpreter");
	CHECK(vk.rfind("https://maps.mail.ru/osm/tools/overpass/api/interpreter?data=%5Bout", 0) == 0);
	CHECK(vk.substr(vk.find("?data=")) == PoisUrl(box).substr(PoisUrl(box).find("?data=")));
	CHECK(PoisUrl(box, "https://x.org/api?apikey=k").find("api?apikey=k&data=") != std::string::npos);
	CHECK(WithKey("https://overpass.geofabrik.de/{key}/api/interpreter", "a/b c") == "https://overpass.geofabrik.de/a%2Fb%20c/api/interpreter");
	CHECK(WithKey("https://x.org/api/interpreter", "k") == "https://x.org/api/interpreter");
}


int
main()
{
	TestRelations();
	TestWikidata();
	TestOverpass();
	TestSettings();
	std::printf("%d checks, %d failures\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
