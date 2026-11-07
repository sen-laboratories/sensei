/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialOverpass.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include <nlohmann/json.hpp>

#include "SpatialWikidata.h"

namespace spatial {

namespace {

using Json = nlohmann::json;

const double kKmPerDegree = 111.32;

/** `de-AT` is `de` */
std::string
BaseLanguage(const std::string& language)
{
	const size_t dash = language.find_first_of("-_");
	return dash == std::string::npos ? language : language.substr(0, dash);
}

std::string
Tag(const Json& tags, const std::string& key)
{
	auto it = tags.find(key);
	return it != tags.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/** the keys that say what a thing is, in the order of preference (a café in a building is a café) */
const char* const kKindKeys[] = {
	"amenity", "shop", "tourism", "historic", "leisure", "office", "craft", "healthcare", "highway", "railway", "public_transport", "aeroway",
	"man_made", "natural", "building", "landuse", "place"
};

/** the roads: a street is a place, but not a point of interest, and it comes in pieces that all have the name. What is a stop or a stand on a road is one. */
bool
IsRoad(const std::string& highway)
{
	return !highway.empty() && highway != "bus_stop" && highway != "platform" && highway != "rest_area" && highway != "services" && highway != "elevator"
		&& highway != "traffic_signals" && highway != "crossing";
}

std::string
WithBlanks(std::string value)
{
	std::replace(value.begin(), value.end(), '_', ' ');
	return value;
}

/**
 * The points that only help to draw a line of a bus or a train on the map: where a vehicle stops on the road or the rails (a stop position, which a tram stop also is), and the edge
 * or area of a platform. The stop itself (a bus stop, a tram stop, a station) is a point of interest.
 */
bool
IsTechnical(const Json& tags)
{
	const std::string transport = Tag(tags, "public_transport");
	const std::string railway = Tag(tags, "railway");
	if (transport == "stop_position" || railway == "platform" || railway == "platform_edge")
		return true;
	return transport == "platform" && Tag(tags, "highway") != "bus_stop" && railway != "tram_stop" && railway != "halt" && railway != "station";
}

/** what the thing is */
void
KindOf(const Json& tags, Poi* poi)
{
	for (const char* key : kKindKeys) {
		const std::string value = Tag(tags, key);
		if (value.empty() || value == "no")
			continue;
		// (building=yes says only that it is one)
		poi->kind = WithBlanks(value == "yes" ? std::string(key) : value);
		poi->kindUri = std::string("https://wiki.openstreetmap.org/wiki/Tag:") + key + "=" + value;
		return;
	}
}

std::string
WikidataOf(const Json& tags)
{
	const std::string id = Tag(tags, "wikidata");
	if (id.size() < 2 || id[0] != 'Q')
		return std::string();
	for (size_t i = 1; i < id.size(); i++) {
		if (id[i] < '0' || id[i] > '9')
			return std::string();
	}
	return id;
}

double
Number(const Json& object, const char* key, bool* found)
{
	auto it = object.find(key);
	if (it == object.end() || !it->is_number()) {
		*found = false;
		return 0;
	}
	return it->get<double>();
}

}	// namespace


Box
PoiBox(const Box& place)
{
	Box none;
	if (!place.valid)
		return none;
	const double middleLatitude = (place.south + place.north) / 2;
	const double middleLongitude = (place.west + place.east) / 2;
	const double kmPerDegreeLongitude = kKmPerDegree * std::max(0.01, std::cos(middleLatitude * M_PI / 180));
	const double heightKm = (place.north - place.south) * kKmPerDegree;
	const double widthKm = (place.east - place.west) * kmPerDegreeLongitude;
	if (heightKm > kPoiMaxKm || widthKm > kPoiMaxKm)
		return none;
	const double halfHeight = std::max(heightKm, kPoiMinKm) / 2 / kKmPerDegree;
	const double halfWidth = std::max(widthKm, kPoiMinKm) / 2 / kmPerDegreeLongitude;
	Box box;
	box.south = middleLatitude - halfHeight;
	box.north = middleLatitude + halfHeight;
	box.west = middleLongitude - halfWidth;
	box.east = middleLongitude + halfWidth;
	box.valid = true;
	return box;
}


std::string
PoisUrl(const Box& box)
{
	char area[128];
	// (Overpass: south, west, north, east; the position is rounded to 11 cm, which also keeps the same area one question for the cache)
	std::snprintf(area, sizeof(area), "(%.6f,%.6f,%.6f,%.6f)", box.south, box.west, box.north, box.east);
	return "https://overpass-api.de/api/interpreter?data=" + PercentEncode(std::string("[out:json][timeout:25];nwr[name]") + area + ";out center;");
}


bool
ParsePois(const std::string& text, const std::string& language, std::vector<Poi>* pois)
{
	Json json = Json::parse(text, NULL, false);
	if (!json.is_object())
		return false;
	auto elements = json.find("elements");
	if (elements == json.end() || !elements->is_array())
		return false;		// (an answer that ran out of time or memory has a "remark" and none)

	const std::string localName = "name:" + BaseLanguage(language);
	std::vector<Poi> found;
	std::set<std::string> seen;
	for (const Json& element : *elements) {
		if (!element.is_object())
			continue;
		auto tagsIt = element.find("tags");
		if (tagsIt == element.end() || !tagsIt->is_object())
			continue;
		const Json& tags = *tagsIt;

		Poi poi;
		poi.name = Tag(tags, localName);
		if (poi.name.empty())
			poi.name = Tag(tags, "name");
		if (poi.name.empty() || !tags.contains("name") || IsRoad(Tag(tags, "highway")) || !Tag(tags, "boundary").empty())
			continue;
		auto type = element.find("type");
		auto id = element.find("id");
		if (type == element.end() || !type->is_string() || id == element.end() || !id->is_number_integer())
			continue;
		poi.osmType = type->get<std::string>();
		poi.osmId = id->get<int64_t>();

		// the position: a node has it, a way or a relation has its centre
		bool hasLatitude = true, hasLongitude = true;
		if (poi.osmType == "node") {
			poi.latitude = Number(element, "lat", &hasLatitude);
			poi.longitude = Number(element, "lon", &hasLongitude);
		} else {
			auto center = element.find("center");
			if (center == element.end() || !center->is_object())
				continue;
			poi.latitude = Number(*center, "lat", &hasLatitude);
			poi.longitude = Number(*center, "lon", &hasLongitude);
		}
		if (!hasLatitude || !hasLongitude)
			continue;

		if (IsTechnical(tags))
			continue;
		KindOf(tags, &poi);
		// (a name and nothing else - no kind, no address - is a label of the map, not a point of interest)
		const std::string street = Tag(tags, "addr:street");
		if (poi.kind.empty() && street.empty() && Tag(tags, "website").empty() && WikidataOf(tags).empty())
			continue;
		poi.wikidataId = WikidataOf(tags);
		poi.website = Tag(tags, "website");
		if (poi.website.empty())
			poi.website = Tag(tags, "contact:website");
		const std::string number = Tag(tags, "addr:housenumber");
		poi.address = number.empty() ? street : street.empty() ? std::string() : street + " " + number;
		poi.postcode = Tag(tags, "addr:postcode");
		poi.locality = Tag(tags, "addr:city");

		if (seen.insert(OsmKey(poi)).second)
			found.push_back(poi);
	}
	*pois = found;
	return true;
}


std::string
OsmKey(const Poi& poi)
{
	return poi.osmType + "/" + std::to_string(poi.osmId);
}


std::string
OsmUrl(const Poi& poi)
{
	return "https://www.openstreetmap.org/" + OsmKey(poi);
}


std::string
OsmKeyFrom(const std::string& links)
{
	static const char* const kHost = "openstreetmap.org/";
	size_t from = 0;
	while ((from = links.find(kHost, from)) != std::string::npos) {
		from += std::string(kHost).size();
		for (const char* type : {"node", "way", "relation"}) {
			const std::string prefix = std::string(type) + "/";
			if (links.compare(from, prefix.size(), prefix) != 0)
				continue;
			size_t end = from + prefix.size();
			while (end < links.size() && links[end] >= '0' && links[end] <= '9')
				end++;
			if (end > from + prefix.size())
				return links.substr(from, end - from);
		}
	}
	return std::string();
}


double
DistanceKm(double latitudeA, double longitudeA, double latitudeB, double longitudeB)
{
	const double radians = M_PI / 180;
	const double dLatitude = (latitudeB - latitudeA) * radians;
	const double dLongitude = (longitudeB - longitudeA) * radians;
	const double a = std::sin(dLatitude / 2) * std::sin(dLatitude / 2)
		+ std::cos(latitudeA * radians) * std::cos(latitudeB * radians) * std::sin(dLongitude / 2) * std::sin(dLongitude / 2);
	return 6371.0 * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

}	// namespace spatial
