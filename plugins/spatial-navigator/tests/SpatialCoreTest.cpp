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

	CHECK(spatial::ParsePois("{\"elements\":[]}", "de", &pois) && pois.empty());			// (nothing there is an answer)
	CHECK(!spatial::ParsePois("{\"remark\":\"runtime error: Query timed out\"}", "de", &pois));
	CHECK(!spatial::ParsePois("not json", "de", &pois));

	CHECK(spatial::OsmKeyFrom("https://www.wikidata.org/wiki/Q1,https://www.openstreetmap.org/way/212496461") == "way/212496461");
	CHECK(spatial::OsmKeyFrom("https://www.openstreetmap.org/relation/9, https://www.openstreetmap.org/node/1") == "relation/9");
	CHECK(spatial::OsmKeyFrom("https://www.openstreetmap.org/user/7").empty() && spatial::OsmKeyFrom("https://www.openstreetmap.org/node/").empty());
	CHECK(std::abs(spatial::DistanceKm(48.2085, 16.3731, 48.2085, 16.3731)) < 1e-9);
	CHECK(std::abs(spatial::DistanceKm(48.2085, 16.3731, 47.0707, 15.4395) - 144.6) < 0.5);		// (Vienna - Graz)
}


static void
TestSettings()
{
	using namespace spatial;
	// the default is the public server; the mirrors and the custom one are chosen by their id
	Settings settings;
	CHECK(OverpassEndpoint(settings) == "https://overpass-api.de/api/interpreter" && OverpassKey(settings).empty());
	ParseSettings("# a comment\noverpass_mirror = vk-maps\n", &settings);
	CHECK(OverpassEndpoint(settings) == "https://maps.mail.ru/osm/tools/overpass/api/interpreter");
	ParseSettings("overpass_mirror = private-coffee", &settings);
	CHECK(OverpassEndpoint(settings) == "https://overpass.private.coffee/api/interpreter");
	// the last choice is the custom one; with no address it is the default; the key belongs to it alone
	ParseSettings("overpass_mirror = custom\noverpass_key = secret123", &settings);
	CHECK(OverpassEndpoint(settings) == kDefaultOverpassUrl && OverpassKey(settings) == "secret123");
	ParseSettings("overpass_url = https://overpass.example.org/api/interpreter", &settings);
	CHECK(OverpassEndpoint(settings) == "https://overpass.example.org/api/interpreter");
	ParseSettings("overpass_mirror = vk-maps", &settings);
	CHECK(OverpassKey(settings).empty());
	// what is not valid is ignored: an unknown mirror, an address that is none
	ParseSettings("overpass_mirror = nowhere\noverpass_url = ftp://x\n", &settings);
	CHECK(settings.overpassMirror == "vk-maps" && settings.overpassUrl == "https://overpass.example.org/api/interpreter");
	const std::vector<Mirror>& mirrors = Mirrors();
	CHECK(mirrors.size() == 4 && std::string(mirrors.back().id) == "custom");		// (the last choice is the custom mirror)

	std::string why;
	CHECK(ValidateSetting("overpass_mirror", "custom") && ValidateSetting("overpass_mirror", "default"));
	CHECK(!ValidateSetting("overpass_mirror", "other", &why) && !why.empty());
	CHECK(ValidateSetting("overpass_url", "") && ValidateSetting("overpass_url", "http://localhost:8080/api/interpreter"));
	CHECK(!ValidateSetting("overpass_url", "overpass.example.org") && !ValidateSetting("overpass_url", "https://a b"));
	CHECK(!ValidateSetting("overpass_url", "https://example.org/#x") && !ValidateSetting("overpass_key", "a b") && ValidateSetting("overpass_key", ""));
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
