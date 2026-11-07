/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// Tests of core/: no Haiku and no GEOS needed, no network (the answer of Wikidata is recorded in tests/fixtures). Build and run with tests/run.sh.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "SpatialRelations.h"
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


int
main()
{
	TestRelations();
	TestWikidata();
	std::printf("%d checks, %d failures\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
