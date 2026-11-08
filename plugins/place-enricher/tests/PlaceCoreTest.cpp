/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// Tests of core/: no Haiku needed, and no network (the answers of the services are recorded in tests/fixtures). Build and run with tests/run.sh.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../../../common/HttpCache.h"
#include "PlaceNominatim.h"
#include "PlaceSettings.h"
#include "PlaceWikidata.h"
#include "../../../common/SettingsText.h"

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

static bool
Has(const std::vector<std::string>& list, const std::string& value)
{
	for (const std::string& each : list) {
		if (each == value)
			return true;
	}
	return false;
}

static bool
Contains(const std::string& text, const std::string& part)
{
	return text.find(part) != std::string::npos;
}


static void
TestAddresses()
{
	CHECK(place::PercentEncode("Café Central, Wien") == "Caf%C3%A9%20Central%2C%20Wien");
	CHECK(place::PercentEncode("a-b_c.d~e") == "a-b_c.d~e");

	const std::string search = place::SearchUrl("Stephansdom Wien", "de", true);
	CHECK(search.compare(0, 52, "https://nominatim.openstreetmap.org/search?q=Stephan") == 0);
	CHECK(Contains(search, "q=Stephansdom%20Wien"));
	CHECK(Contains(search, "&format=jsonv2"));
	CHECK(Contains(search, "&addressdetails=1&extratags=1&namedetails=1"));
	CHECK(Contains(search, "&polygon_text=1&polygon_threshold=0.0001"));
	CHECK(Contains(place::SearchUrl("Wien", "", true, 8, 0.002), "&polygon_threshold=0.002"));
	CHECK(Contains(place::LookupUrl("relation", 1, "", true, 0.01), "&polygon_threshold=0.01"));
	CHECK(Contains(place::ReverseUrl(1, 2, "", true, 0.005), "&polygon_threshold=0.005"));
	CHECK(Contains(search, "&accept-language=de"));
	CHECK(Contains(search, "&limit=8"));
	const std::string plain = place::SearchUrl("Wien", "", false, 3);
	CHECK(!Contains(plain, "polygon"));
	CHECK(!Contains(plain, "accept-language"));
	CHECK(Contains(plain, "&limit=3"));

	CHECK(Contains(place::LookupUrl("way", 212496461, "de-AT", false), "/lookup?osm_ids=W212496461&"));
	CHECK(Contains(place::LookupUrl("relation", 109166, "", false), "osm_ids=R109166"));
	CHECK(Contains(place::LookupUrl("node", 5, "de-AT", false), "accept-language=de-AT"));
	const std::string reverse = place::ReverseUrl(48.2084923, 16.3731269, "", false);
	CHECK(Contains(reverse, "/reverse?lat=48.2084923&lon=16.3731269&"));
	CHECK(Contains(reverse, "&layer=address&"));

	// the name of the file of a place: a building has an address and no name
	{
		place::Hit named, building, bare;
		named.name = "Karlskirche";
		building.road = "Anton-Langer-Gasse";
		building.houseNumber = "66";
		bare.displayName = "66, Anton-Langer-Gasse, Wien";
		CHECK(place::NameFor(named) == "Karlskirche");
		CHECK(place::NameFor(building) == "Anton-Langer-Gasse 66");
		building.houseNumber = "";
		CHECK(place::NameFor(building) == "Anton-Langer-Gasse");
		CHECK(place::NameFor(bare) == "66");
	}
	// near: a small place close by, not a city that contains the position and not one that is far
	{
		place::Position home{48.1689, 16.2826};
		place::Hit shop, city, far;
		shop.hasBox = true; shop.south = 48.1692; shop.north = 48.1694; shop.west = 16.2829; shop.east = 16.2832;		// about 40 metres away
		shop.latitude = 48.1693; shop.longitude = 16.2830;
		city.hasBox = true; city.south = 48.1; city.north = 48.3; city.west = 16.1; city.east = 16.6;				// contains the position, 30 km across
		far.hasBox = true; far.south = 48.2000; far.north = 48.2002; far.west = 16.3000; far.east = 16.3002;		// kilometres away
		CHECK(place::IsNear(shop, home, 0.25, 2.0));
		CHECK(!place::IsNear(city, home, 0.25, 2.0));
		CHECK(!place::IsNear(far, home, 0.25, 2.0));
		CHECK(place::ExtentKm(city) > 20 && place::ExtentKm(shop) < 0.1);
	}
	CHECK(Contains(place::ReverseUrl(-33.5, -70.25, "", false), "lat=-33.5000000&lon=-70.2500000"));
}


static void
TestParse()
{
	std::vector<place::Hit> hits;
	CHECK(place::ParseHits(Fixture("nominatim-search-stephansdom.json"), &hits));
	CHECK(hits.size() == 1);
	if (hits.empty())
		return;
	const place::Hit& dom = hits[0];
	CHECK(dom.osmType == "way" && dom.osmId == 212496461);
	CHECK(dom.name == "Stephansdom");
	CHECK(dom.latitude > 48.2084 && dom.latitude < 48.2086);
	CHECK(dom.longitude > 16.3731 && dom.longitude < 16.3732);
	CHECK(dom.hasBox && dom.south < dom.north && dom.west < dom.east);
	CHECK(dom.south > 48.208 && dom.north < 48.209);
	CHECK(dom.category == "amenity" && dom.type == "place_of_worship" && dom.addressType == "amenity");
	CHECK(dom.wikidataId == "Q5943" && dom.wikipedia == "de:Stephansdom");
	CHECK(dom.website == "https://www.stephanskirche.at/");
	CHECK(dom.road == "Stephansplatz" && dom.houseNumber == "3" && dom.postcode == "1010");
	CHECK(dom.locality == "Wien" && dom.countryCode == "AT" && dom.country == "Österreich");
	CHECK(dom.wkt.compare(0, 8, "POLYGON(") == 0);
	// the other names: the English one and the alternative names first, never the name itself, each once, at most 12
	CHECK(!dom.otherNames.empty() && dom.otherNames.size() <= 12);
	CHECK(dom.otherNames[0] == "Saint Stephen's Cathedral");
	CHECK(Has(dom.otherNames, "Steffl"));
	CHECK(Has(dom.otherNames, "Dom- und Metropolitankirche St. Stephan"));
	CHECK(!Has(dom.otherNames, "Stephansdom"));

	std::vector<place::Hit> wien;
	CHECK(place::ParseHits(Fixture("nominatim-search-wien.json"), &wien));
	CHECK(wien.size() == 1);
	if (!wien.empty()) {
		CHECK(wien[0].osmType == "relation" && wien[0].osmId == 109166);
		CHECK(wien[0].category == "boundary" && wien[0].type == "administrative" && wien[0].addressType == "city");
		CHECK(wien[0].wikidataId == "Q1741");
		CHECK(wien[0].road.empty() && wien[0].countryCode == "AT");
		CHECK(wien[0].importance > 0.8);
		CHECK(!wien[0].wkt.empty());
	}

	// an empty list is an answer; text that is not one is not
	std::vector<place::Hit> none{place::Hit()};
	CHECK(place::ParseHits("[]", &none) && none.empty());
	CHECK(!place::ParseHits("not json", &none));
	CHECK(!place::ParseHits("{\"error\": \"x\"}", &none));
	place::Hit one;
	CHECK(!place::ParseHit("{\"error\":\"Unable to geocode\"}", &one));
	CHECK(!place::ParseHit("[]", &one));
	CHECK(!place::ParseHit("", &one));
	// the answer of a reverse lookup is one object: the first of the fixture's list, as it is
	CHECK(place::ParseHit("{\"osm_type\":\"node\",\"osm_id\":7,\"lat\":\"1.5\",\"lon\":\"2.5\",\"display_name\":\"Somewhere, There\",\"category\":\"place\",\"type\":\"village\"}", &one));
	CHECK(one.osmId == 7 && one.latitude == 1.5 && one.longitude == 2.5 && one.name == "Somewhere");
	// a house that has no name: its address is its name
	place::Hit house;
	CHECK(place::ParseHit("{\"osm_type\":\"way\",\"osm_id\":8,\"lat\":\"48.2\",\"lon\":\"16.3\",\"addresstype\":\"place\",\"display_name\":\"4, Naglergasse, Wien\","
		"\"address\":{\"house_number\":\"4\",\"road\":\"Naglergasse\",\"city\":\"Wien\",\"country\":\"Österreich\",\"country_code\":\"at\"}}", &house));
	CHECK(house.name == "Naglergasse 4");
	// a house number alone is no name: the first part of the line of Nominatim
	CHECK(place::ParseHit("{\"osm_type\":\"way\",\"osm_id\":9,\"lat\":\"1\",\"lon\":\"2\",\"display_name\":\"Rathausplatz, Wien\",\"address\":{\"house_number\":\"1\"}}", &house) && house.name == "Rathausplatz");
}


static void
TestWhatToMakeOfThem()
{
	std::vector<place::Hit> hits, wien;
	place::ParseHits(Fixture("nominatim-search-stephansdom.json"), &hits);
	place::ParseHits(Fixture("nominatim-search-wien.json"), &wien);
	if (hits.empty() || wien.empty())
		return;
	const place::Hit& dom = hits[0];

	CHECK(place::OsmUrl(dom) == "https://www.openstreetmap.org/way/212496461");
	const std::vector<std::string> links = place::SameAs(dom);
	CHECK(links.size() == 3);
	CHECK(Has(links, "https://www.openstreetmap.org/way/212496461"));
	CHECK(Has(links, "https://www.wikidata.org/wiki/Q5943"));
	CHECK(Has(links, "https://de.wikipedia.org/wiki/Stephansdom"));

	// the links are found again in a list of any kind (separated by commas, with the links of other kinds)
	std::string list = "https://example.org/a, ";
	for (const std::string& link : links)
		list += link + ",";
	std::string type;
	int64_t id = 0;
	CHECK(place::OsmFrom(list, &type, &id) && type == "way" && id == 212496461);
	CHECK(place::WikidataFrom(list) == "Q5943");
	CHECK(!place::OsmFrom("https://www.themoviedb.org/movie/603", &type, &id));
	CHECK(place::WikidataFrom("https://www.themoviedb.org/movie/603").empty());
	CHECK(place::WikidataFrom("https://www.wikidata.org/wiki/Property:P18").empty());
	CHECK(place::OsmFrom("https://www.openstreetmap.org/relation/109166", &type, &id) && type == "relation" && id == 109166);
	CHECK(place::OsmFrom("https://www.openstreetmap.org/node/0", &type, &id) == false);

	// a title with a comma or a mark must not break the list
	place::Hit paris;
	paris.wikipedia = "en:Paris, Texas";
	CHECK(place::SameAs(paris).size() == 1 && place::SameAs(paris)[0] == "https://en.wikipedia.org/wiki/Paris%2C_Texas");
	paris.wikipedia = "de:Café Central";
	CHECK(place::SameAs(paris)[0] == "https://de.wikipedia.org/wiki/Caf%C3%A9_Central");

	// a place that was made from Wikidata is found among the answers of its name by its Wikidata item
	std::vector<place::Hit> both = hits;
	both.insert(both.end(), wien.begin(), wien.end());
	CHECK(place::FindByWikidata(both, "Q1741") != nullptr && place::FindByWikidata(both, "Q1741")->name == wien[0].name);
	CHECK(place::FindByWikidata(both, "Q5943") != nullptr && place::FindByWikidata(both, "Q5943")->osmId == 212496461);
	CHECK(place::FindByWikidata(both, "Q40") == nullptr);
	CHECK(place::FindByWikidata(both, "") == nullptr);

	CHECK(!place::IsAdministrative(dom));
	CHECK(place::IsAdministrative(wien[0]));
	CHECK(place::PlaceTypeIri(dom) == "https://wiki.openstreetmap.org/wiki/Tag:amenity=place_of_worship");
	CHECK(place::PlaceTypeIri(wien[0]) == "https://wiki.openstreetmap.org/wiki/Tag:place=city");
	place::Hit nothing;
	CHECK(place::PlaceTypeIri(nothing).empty());
	// the type in words
	CHECK(place::PlaceTypeName(dom) == "place of worship");
	CHECK(place::PlaceTypeName(wien[0]) == "city");
	CHECK(place::PlaceTypeName(nothing).empty());
	place::Hit building = nothing;
	building.category = "building"; building.type = "yes";
	CHECK(place::PlaceTypeName(building) == "building");
	building.category = "tourism"; building.type = "guest_house";
	CHECK(place::PlaceTypeName(building) == "guest house");
	building.category = "boundary"; building.type = "administrative"; building.addressType = "";
	CHECK(place::PlaceTypeName(building) == "administrative area");
	building.addressType = "state_district";
	CHECK(place::PlaceTypeName(building) == "state district");

	CHECK(place::AddressLine(dom) == "Stephansplatz 3, 1010 Wien, Österreich");
	CHECK(place::AddressLine(wien[0]).empty());		// (a city has no postal address)

	CHECK(place::TitleMatches(dom, "Stephansdom"));
	CHECK(place::TitleMatches(dom, "stephansdom wien"));		// (the name that is held by the question)
	CHECK(place::TitleMatches(dom, "Steffl"));				// (an other name)
	CHECK(place::TitleMatches(dom, ""));
	CHECK(!place::TitleMatches(dom, "Schönbrunn"));
	CHECK(place::TitleMatches(dom, "Stephans"));				// (a part of the name)
	CHECK(place::TitleMatches(dom, "Stephansdom Innere Stadt"));	// (what else is asked for says where: it is in the address)
	CHECK(place::TitleMatches(dom, "Stephansdom Österreich"));
	CHECK(!place::TitleMatches(dom, "Stephansdom 0001"));		// (a number that is no part of the place)
	CHECK(!place::TitleMatches(dom, "Stephansdom Graz"));		// (another city)
	place::Hit office;											// (a photo called IMG_0001.jpg is searched as "IMG 0001")
	office.name = "IMG";
	office.displayName = "IMG, Sayram, Tashkent, Uzbekistan";
	CHECK(!place::TitleMatches(office, "IMG 0001"));
	CHECK(place::TitleMatches(office, "IMG"));
	CHECK(place::TitleMatches(office, "IMG Tashkent"));
	CHECK(place::ScoreHit(dom, 0, 1, "Stephansdom") > place::ScoreHit(dom, 0, 1, "Stephansdom Wien"));
	CHECK(place::ScoreHit(dom, 0, 4, "Stephansdom") > place::ScoreHit(dom, 3, 4, "Stephansdom"));
	CHECK(place::ScoreHit(dom, 0, 1, "Schönbrunn") < place::ScoreHit(dom, 0, 1, "Stephansdom"));
	CHECK(place::ScoreHit(wien[0], 0, 1, "Wien") > 0.9f);

	// a place with an outline too large to keep is asked for again with a coarser tolerance: 0.5% of its size, between the usual one and 0.01 degrees
	place::Hit country;
	CHECK(place::BoundaryTolerance(country) == place::kBoundaryTolerance);				// (no box: the usual one)
	CHECK(place::BoundaryTolerance(dom) == place::kBoundaryTolerance);					// (a building: the usual one)
	CHECK(place::BoundaryTolerance(wien[0]) > place::kBoundaryTolerance && place::BoundaryTolerance(wien[0]) < 0.003);	// (a city, 0.4 degrees)
	country.hasBox = true;
	country.south = 46.4; country.north = 49.0; country.west = 9.5; country.east = 17.2;		// (Austria: 7.7 degrees)
	CHECK(place::BoundaryTolerance(country) > 0.0075 && place::BoundaryTolerance(country) <= 0.01);
	country.west = -180; country.east = 180;											// (the whole world: never more than 0.01)
	CHECK(place::BoundaryTolerance(country) == 0.01);

	// the answer in English calls Vienna "Vienna", the file is called "Wien": that is the name in the language of the place, in `namedetails`
	place::Hit vienna;
	CHECK(place::ParseHit("{\"osm_type\":\"relation\",\"osm_id\":109166,\"lat\":\"48.2\",\"lon\":\"16.3\",\"name\":\"Vienna\",\"display_name\":\"Vienna, Austria\","
		"\"namedetails\":{\"name\":\"Wien\",\"name:en\":\"Vienna\",\"name:de\":\"Wien\",\"name:fr\":\"Vienne\"}}", &vienna));
	CHECK(vienna.name == "Vienna");
	CHECK(Has(vienna.otherNames, "Wien") && Has(vienna.otherNames, "Vienne") && !Has(vienna.otherNames, "Vienna"));
	CHECK(place::TitleMatches(vienna, "Wien"));
	CHECK(place::ScoreHit(vienna, 0, 1, "Wien") > 0.6f);

	CHECK(place::OtherNames("a", {"a", "b", "b", "", "c"}, 5).size() == 2);
	CHECK(place::OtherNames("a", {"b", "c", "d"}, 2).size() == 2);
}


static void
TestWikidata()
{
	CHECK(Contains(place::EntityUrl("Q5943", "de"), "ids=Q5943"));
	CHECK(Contains(place::EntityUrl("Q5943", "de"), "&languages=de%7Cen&languagefallback=1"));
	CHECK(Contains(place::EntityUrl("Q5943", "de-AT"), "&languages=de%7Cen&"));
	CHECK(Contains(place::EntityUrl("Q5943", "en"), "&languages=en&"));
	CHECK(Contains(place::EntityUrl("Q5943", ""), "&languages=en&"));

	place::Entity dom, wien;
	CHECK(place::ParseEntity(Fixture("wikidata-Q5943.json"), "Q5943", "de", &dom));
	CHECK(dom.label == "Stephansdom");
	CHECK(dom.description == "Kathedrale des Erzbistums Wien");
	CHECK(dom.photo == "Wien - Stephansdom (1).JPG");
	CHECK(dom.coatOfArms.empty() && dom.flag.empty());
	CHECK(place::ParseEntity(Fixture("wikidata-Q5943.json"), "Q5943", "fr", &dom));		// (a language that it has no description in: English)
	CHECK(dom.description == "church in Vienna, Austria");

	CHECK(place::ParseEntity(Fixture("wikidata-Q1741.json"), "Q1741", "de-AT", &wien));
	CHECK(wien.description == "Bundeshauptstadt und bevölkerungsreichste Stadt der Republik Österreich");
	CHECK(wien.coatOfArms == "Wien Wappen.svg");
	CHECK(!wien.flag.empty() && !wien.locatorMap.empty() && !wien.photo.empty());

	place::Entity other;
	CHECK(!place::ParseEntity(Fixture("wikidata-Q5943.json"), "Q1", "de", &other));
	CHECK(!place::ParseEntity("not json", "Q1", "de", &other));
	CHECK(!place::ParseEntity("{\"entities\":{\"Q1\":{\"id\":\"Q1\",\"missing\":\"\"}}}", "Q1", "de", &other));

	// the picture that stands for the place
	place::Image area = place::PickImage(wien, true);
	CHECK(area.file == "Wien Wappen.svg" && area.kind == "coat of arms");
	place::Image building = place::PickImage(dom, false);
	CHECK(building.file == "Wien - Stephansdom (1).JPG" && building.kind == "photo");
	CHECK(place::PickImage(dom, true).kind == "photo");			// (an area with nothing but a photo)
	CHECK(place::PickImage(wien, false).kind == "photo");		// (a building has no coat of arms)
	place::Entity bare;
	CHECK(place::PickImage(bare, true).file.empty());
	bare.logo = "Logo.svg";
	CHECK(place::PickImage(bare, false).kind == "logo" && place::PickImage(bare, true).file.empty());
	bare.flag = "Flag.svg";
	bare.locatorMap = "Map.svg";
	CHECK(place::PickImage(bare, true).kind == "flag");			// (the flag before the map)
}


static void
TestCommons()
{
	const std::string url = place::ImageInfoUrl("Wien Wappen.svg", 256);
	CHECK(Contains(url, "https://commons.wikimedia.org/w/api.php?action=query&titles=File%3AWien%20Wappen.svg"));
	CHECK(Contains(url, "&iiurlwidth=256"));
	CHECK(Contains(url, "iiprop=url%7Cextmetadata%7Cmime%7Csize"));

	place::Image wappen;
	CHECK(place::ParseImageInfo(Fixture("commons-wien-wappen.json"), &wappen));
	CHECK(wappen.file == "Wien Wappen.svg");
	CHECK(wappen.thumbnailUrl.compare(0, 29, "https://thumb.wikimedia.org/w") == 0);
	CHECK(Contains(wappen.thumbnailUrl, ".svg.png"));						// (a vector image comes as a PNG)
	CHECK(wappen.license == "Public domain" && !wappen.attributionRequired);
	CHECK(wappen.pageUrl.compare(0, 36, "https://commons.wikimedia.org/wiki/F") == 0);
	CHECK(place::CreditOf(wappen) == "Public domain (Wikimedia Commons)");

	place::Image photo;
	CHECK(place::ParseImageInfo(Fixture("commons-stephansdom.json"), &photo));
	CHECK(photo.mime == "image/jpeg");
	CHECK(photo.license == "CC BY-SA 4.0" && photo.attributionRequired);
	CHECK(photo.artist == "C.Stadler/Bwag");					// (the author is a link at Commons: only its text)
	CHECK(place::CreditOf(photo) == "C.Stadler/Bwag, CC BY-SA 4.0 (Wikimedia Commons)");

	place::Image none;
	CHECK(!place::ParseImageInfo("not json", &none));
	CHECK(!place::ParseImageInfo("{\"query\":{\"pages\":{\"-1\":{\"title\":\"File:X.png\",\"missing\":\"\"}}}}", &none));
	CHECK(place::CreditOf(none).empty());

	CHECK(place::StripHtml("<a href=\"//x\" title=\"User:Bwag\">C.Stadler/Bwag</a>") == "C.Stadler/Bwag");
	CHECK(place::StripHtml("  Tom &amp; Jerry&nbsp;&lt;3 &#233;&#x20AC; ") == "Tom & Jerry <3 \xC3\xA9\xE2\x82\xAC");
	CHECK(place::StripHtml("a <b>b</b>\n  <i>c</i>") == "a b c");
	CHECK(place::StripHtml("fish & chips") == "fish & chips");
	CHECK(place::StripHtml("").empty());
}


static void
TestPosition()
{
	std::vector<place::Hit> hits, wien;
	place::ParseHits(Fixture("nominatim-search-stephansdom.json"), &hits);
	place::ParseHits(Fixture("nominatim-search-wien.json"), &wien);
	if (hits.empty() || wien.empty())
		return;
	const place::Hit& dom = hits[0];
	place::Position here;						// (in front of the cathedral)
	here.latitude = 48.2085; here.longitude = 16.3731;
	place::Position home;						// (Karlsplatz, 800 m away from it)
	home.latitude = 48.2005; home.longitude = 16.3697;
	place::Position salzburg;
	salzburg.latitude = 47.8095; salzburg.longitude = 13.0550;

	// the distance: 0 inside the bounding box, else to the nearest point of it
	CHECK(place::DistanceKm(dom, here) == 0.0);
	CHECK(place::DistanceKm(dom, home) > 0.7 && place::DistanceKm(dom, home) < 1.0);
	CHECK(place::DistanceKm(wien[0], here) == 0.0);						// (inside the box of the city)
	CHECK(place::DistanceKm(wien[0], salzburg) > 200 && place::DistanceKm(wien[0], salzburg) < 260);
	place::Hit point;											// (no box: the centre)
	point.latitude = 48.0; point.longitude = 16.0;
	place::Position onePointOneDegreeNorth;
	onePointOneDegreeNorth.latitude = 49.0; onePointOneDegreeNorth.longitude = 16.0;
	CHECK(place::DistanceKm(point, onePointOneDegreeNorth) > 111.0 && place::DistanceKm(point, onePointOneDegreeNorth) < 111.4);	// (one degree of latitude)

	CHECK(place::DistanceText(0.02) == "at the position");
	CHECK(place::DistanceText(0.35) == "350 m");
	CHECK(place::DistanceText(2.34) == "2.3 km");
	CHECK(place::DistanceText(123.4) == "123 km");
	CHECK(place::DistanceText(9.96) == "10.0 km");

	// the name of a file: a name has more letters than digits
	CHECK(place::LooksLikeName("Stephansdom"));
	CHECK(place::LooksLikeName("Café Central"));
	CHECK(place::LooksLikeName("Route 66"));
	CHECK(place::LooksLikeName("Österreich"));
	CHECK(place::LooksLikeName("Wien 1"));
	CHECK(!place::LooksLikeName("IMG 0001"));
	CHECK(!place::LooksLikeName("DSC00412"));
	CHECK(!place::LooksLikeName("2023-05-01 14.32.07"));
	CHECK(!place::LooksLikeName("PXL_20230501_143207"));
	CHECK(!place::LooksLikeName(""));
	CHECK(!place::LooksLikeName("A"));

	// the position ranks the places of one name: Cathedral of Saint Stephen in Vienna and one with the same name far away
	place::Hit farAway = dom;
	farAway.osmId = 1;
	farAway.latitude = 40.7; farAway.longitude = -74.0;
	farAway.south = 40.699; farAway.north = 40.701; farAway.west = -74.001; farAway.east = -73.999;
	CHECK(place::ScoreHitNear(dom, 1, 2, "Stephansdom", here) > place::ScoreHitNear(farAway, 0, 2, "Stephansdom", here));	// (also when the far one comes first)
	CHECK(place::ScoreHitNear(farAway, 0, 2, "Stephansdom", place::Position{40.7, -74.0}) > place::ScoreHitNear(dom, 1, 2, "Stephansdom", place::Position{40.7, -74.0}));
	CHECK(place::ScoreHitNear(dom, 0, 1, "Stephansdom", here) > place::ScoreHitNear(dom, 0, 1, "Stephansdom", salzburg));
	CHECK(place::ScoreHitNear(wien[0], 0, 1, "Wien", here) > 0.9f);							// (a photo that was taken in a city)
	CHECK(place::ScoreHitNear(dom, 0, 1, "Schönbrunn", here) < place::ScoreHitNear(dom, 0, 1, "Stephansdom", here));
}


static void
TestCache()
{
	const std::string base = std::string(std::getenv("TMPDIR") != nullptr ? std::getenv("TMPDIR") : "/tmp") + "/place-cache-test-" + std::to_string((long)std::rand());
	{
		sensei::DiskCache cache(base + "/answers");
		std::string body;
		CHECK(!cache.Get("https://x/a", &body, -1));
		cache.Put("https://x/a", "line one\nline two\n");
		CHECK(cache.Get("https://x/a", &body, -1) && body == "line one\nline two\n");
		CHECK(!cache.Get("https://x/b", &body, -1));
		cache.Put("https://x/a", "new");
		CHECK(cache.Get("https://x/a", &body, 3600) && body == "new");
		CHECK(sensei::DiskCache::FileName("https://x/a").size() == 16);
		CHECK(sensei::DiskCache::FileName("https://x/a") == sensei::DiskCache::FileName("https://x/a"));
		CHECK(sensei::DiskCache::FileName("https://x/a") != sensei::DiskCache::FileName("https://x/b"));
		// a file of another address with the same name is no hit
		{
			std::ofstream clash(base + "/answers/" + sensei::DiskCache::FileName("https://x/c"));
			clash << "https://x/other\nbody";
		}
		CHECK(!cache.Get("https://x/c", &body, -1));
		sensei::DiskCache nowhere("");
		nowhere.Put("k", "v");
		CHECK(!nowhere.Get("k", &body, -1));
	}
	{
		sensei::RateLimiter limiter(base + "/stamp/last", 300);
		CHECK(limiter.Wait() == 0);						// (the first request: no waiting)
		const long waited = limiter.Wait();				// (the second at once: the rest of the 300 ms)
		CHECK(waited > 150 && waited <= 300);
	}
	std::system(("rm -rf '" + base + "'").c_str());
}


static void
TestSettings()
{
	place::Settings s;
	CHECK(s.language.empty() && !s.lookupByPosition && s.picture && s.boundary);		// (the position of a file is not sent unless the user says so)
	place::ParseSettings("language = de-AT\nlookup_by_position = yes\npicture = no\nboundary = no  # large\nunknown = 1\n", &s);
	CHECK(s.language == "de-AT" && s.lookupByPosition && !s.picture && !s.boundary);
	place::ParseSettings("language = Deutsch\nlookup_by_position = maybe\n", &s);
	CHECK(s.language == "de-AT" && s.lookupByPosition);									// (bad values leave what is there)									// (bad values leave what is there)
	place::ParseSettings("language = \n", &s);
	CHECK(s.language.empty());

	std::string why;
	CHECK(place::ValidateSetting("language", "de", &why));
	CHECK(place::ValidateSetting("language", "", &why));
	CHECK(place::ValidateSetting("language", "pt-BR", &why));
	CHECK(!place::ValidateSetting("language", "german", &why) && !why.empty());
	CHECK(!place::ValidateSetting("language", "de # x", &why));
	CHECK(place::ValidateSetting("lookup_by_position", "no", &why));
	CHECK(!place::ValidateSetting("lookup_by_position", "maybe", &why));
	CHECK(!place::ValidateSetting("picture", "maybe", &why));
	CHECK(!place::ValidateSetting("nothing", "x", &why) && why == "no such setting");
}


// A file that says where it is (its city, region, country) is searched for in that country: "Westbahnhof" in Wien is not the station in Budapest, which has the name in German, too.
static void
TestArea()
{
	// the search that places the city: the city (with the region and the country if it is named), else the region, else the country; a country as a code only restricts
	const std::string wienUrl = place::AreaUrl("Wien", "", "", "");
	CHECK(Contains(wienUrl, "/search?q=Wien&limit=1&featureType=settlement"));
	CHECK(!Contains(wienUrl, "countrycodes"));
	CHECK(Contains(place::AreaUrl("Wien", "", "AT", ""), "&countrycodes=at"));
	CHECK(Contains(place::AreaUrl("Wien", "", "AT", ""), "q=Wien&"));
	CHECK(Contains(place::AreaUrl("Wien", "", "Austria", ""), "q=Wien%2C%20Austria&limit=1&featureType=settlement"));
	CHECK(Contains(place::AreaUrl("Springfield", "Illinois", "United States", "en"), "q=Springfield%2C%20Illinois%2C%20United%20States"));
	CHECK(Contains(place::AreaUrl("", "Tirol", "", ""), "featureType=state"));
	CHECK(Contains(place::AreaUrl("", "", "Austria", ""), "q=Austria&limit=1&featureType=country"));
	CHECK(place::AreaUrl("", "", "AT", "").empty());			// (a code is no name to look up: it is the country)
	CHECK(place::AreaUrl("", "", "", "").empty());
	CHECK(place::AreaUrl("  ", " ", "", "").empty());

	// what the answer says: the country and the box of the city
	std::vector<place::Hit> wien;
	CHECK(place::ParseHits(Fixture("nominatim-search-wien.json"), &wien));
	place::Area area;
	CHECK(place::AreaFrom(wien, "", &area));
	CHECK(area.countryCode == "AT" && area.hasBox);
	CHECK(area.south < 48.2 && area.north > 48.2 && area.west < 16.37 && area.east > 16.37);
	// a country that is a code is the country, found or not; nothing known is nothing
	place::Area bare;
	CHECK(place::AreaFrom(std::vector<place::Hit>(), "at", &bare) && bare.countryCode == "AT" && !bare.hasBox);
	place::Area none;
	CHECK(!place::AreaFrom(std::vector<place::Hit>(), "", &none));
	CHECK(!place::AreaFrom(std::vector<place::Hit>(), "Austria", &none));

	// the search for the name in the area: only the country, and the box ranks (it does not limit)
	const std::string search = place::SearchUrl("Westbahnhof", "", false, 8, place::kBoundaryTolerance, area);
	CHECK(Contains(search, "/search?q=Westbahnhof&limit=8&countrycodes=at&viewbox=16.1818310,48.3226679,16.5775132,48.1179069"));
	CHECK(!Contains(search, "bounded"));
	CHECK(!Contains(place::SearchUrl("Westbahnhof", "", false, 8, place::kBoundaryTolerance, bare), "viewbox"));
	CHECK(Contains(place::SearchUrl("Westbahnhof", "", false, 8, place::kBoundaryTolerance, bare), "&countrycodes=at"));

	// the answers of another country go; one that says nothing of its country stays
	place::Hit budapest, vienna, unknown;
	budapest.name = "Nyugati pályaudvar"; budapest.countryCode = "HU";
	vienna.name = "Westbahnhof"; vienna.countryCode = "AT";
	unknown.name = "Westbahnhof (no country)";
	std::vector<place::Hit> hits{budapest, vienna, unknown};
	place::KeepInArea(&hits, area);
	CHECK(hits.size() == 2 && hits[0].name == "Westbahnhof" && hits[1].name == "Westbahnhof (no country)");
	std::vector<place::Hit> all{budapest, vienna};
	place::KeepInArea(&all, place::Area());			// (an area without a country keeps all)
	CHECK(all.size() == 2);
}


int
main()
{
	TestArea();
	TestAddresses();
	TestParse();
	TestWhatToMakeOfThem();
	TestWikidata();
	TestCommons();
	TestPosition();
	TestCache();
	TestSettings();
	std::printf("%d checks, %d failures\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
