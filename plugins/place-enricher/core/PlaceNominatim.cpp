/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PlaceNominatim.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include <nlohmann/json.hpp>

namespace place {

namespace {

const char* const kApi = "https://nominatim.openstreetmap.org";
const char* const kOsmTag = "https://wiki.openstreetmap.org/wiki/Tag:";
const size_t kMaxOtherNames = 12;

using Json = nlohmann::json;

std::string
Str(const Json& json, const char* key)
{
	auto it = json.find(key);
	return it != json.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/** a number that Nominatim gives as a number or as a text (`"lat": "48.2084923"`) */
double
Num(const Json& json, const char* key)
{
	auto it = json.find(key);
	if (it == json.end())
		return 0;
	if (it->is_number())
		return it->get<double>();
	if (it->is_string())
		return std::atof(it->get<std::string>().c_str());
	return 0;
}

int64_t
Int(const Json& json, const char* key)
{
	auto it = json.find(key);
	if (it == json.end())
		return 0;
	if (it->is_number())
		return it->get<int64_t>();
	if (it->is_string())
		return std::atoll(it->get<std::string>().c_str());
	return 0;
}

/** letters and digits in lower case, nothing else (bytes of a UTF-8 letter are kept): names compared as a person reads them */
std::string
Plain(const std::string& text)
{
	std::string plain;
	for (unsigned char c : text) {
		if (c >= 0x80)
			plain += (char)c;
		else if (std::isalnum(c))
			plain += (char)std::tolower(c);
	}
	return plain;
}

std::string
Coordinate(double value)
{
	char buffer[40];
	std::snprintf(buffer, sizeof(buffer), "%.7f", value);
	return buffer;
}

/** the parameters that every question has: what the answer holds, and the language */
std::string
Common(const std::string& language, bool boundary, double tolerance)
{
	std::string text = "&format=jsonv2&addressdetails=1&extratags=1&namedetails=1";
	if (boundary) {
		char threshold[32];
		std::snprintf(threshold, sizeof(threshold), "%g", tolerance);
		text += std::string("&polygon_text=1&polygon_threshold=") + threshold;
	}
	if (!language.empty())
		text += "&accept-language=" + PercentEncode(language);
	return text;
}

std::string
FirstOf(const Json& address, std::initializer_list<const char*> keys)
{
	for (const char* key : keys) {
		const std::string value = Str(address, key);
		if (!value.empty())
			return value;
	}
	return std::string();
}

std::string
Upper(std::string text)
{
	for (char& c : text)
		c = (char)std::toupper((unsigned char)c);
	return text;
}

bool
ToHit(const Json& json, Hit* hit)
{
	if (!json.is_object())
		return false;
	Hit h;
	h.osmType = Str(json, "osm_type");
	h.osmId = Int(json, "osm_id");
	if (h.osmType.empty() && h.osmId == 0)
		return false;
	h.name = Str(json, "name");
	h.displayName = Str(json, "display_name");
	h.latitude = Num(json, "lat");
	h.longitude = Num(json, "lon");
	auto box = json.find("boundingbox");
	if (box != json.end() && box->is_array() && box->size() == 4) {
		auto number = [](const Json& value) { return value.is_number() ? value.get<double>() : std::atof(value.get<std::string>().c_str()); };
		h.south = number((*box)[0]);
		h.north = number((*box)[1]);
		h.west = number((*box)[2]);
		h.east = number((*box)[3]);
		h.hasBox = true;
	}
	h.category = Str(json, "category");
	if (h.category.empty())
		h.category = Str(json, "class");
	h.type = Str(json, "type");
	h.addressType = Str(json, "addresstype");
	h.placeRank = (int)Int(json, "place_rank");
	h.importance = Num(json, "importance");
	h.wkt = Str(json, "geotext");

	auto extra = json.find("extratags");
	if (extra != json.end() && extra->is_object()) {
		h.wikidataId = Str(*extra, "wikidata");
		h.wikipedia = Str(*extra, "wikipedia");
		h.website = Str(*extra, "website");
		if (h.website.empty())
			h.website = Str(*extra, "contact:website");
	}

	// the other names: the English one and the alternative names first (the ones that a reader knows), then the other languages by their code. The name in the language of the place
	// (`name`) is one of them when the answer is in another language: \"Wien\" for Vienna, which is what a file of that place is called.
	std::vector<std::string> english, names, later;
	auto details = json.find("namedetails");
	if (details != json.end() && details->is_object()) {
		if (h.name.empty())
			h.name = Str(*details, "name");
		for (auto it = details->begin(); it != details->end(); ++it) {
			const std::string& key = it.key();
			if (!it->is_string())
				continue;
			const bool first = key == "name:en" || key == "name" || key == "alt_name" || key == "loc_name" || key == "official_name" || key == "old_name" || key == "short_name";
			if (!first && key.compare(0, 5, "name:") != 0)
				continue;
			// (alt_name and the like hold several names with a semicolon)
			const std::string value = it->get<std::string>();
			size_t start = 0;
			while (start <= value.size()) {
				size_t end = value.find(';', start);
				if (end == std::string::npos)
					end = value.size();
				std::string part = value.substr(start, end - start);
				const size_t begin = part.find_first_not_of(' ');
				if (begin != std::string::npos)
					(key == "name:en" ? english : (first ? names : later)).push_back(part.substr(begin, part.find_last_not_of(' ') - begin + 1));
				start = end + 1;
			}
		}
	}
	names.insert(names.begin(), english.begin(), english.end());
	names.insert(names.end(), later.begin(), later.end());
	h.otherNames = OtherNames(h.name, names, kMaxOtherNames);

	auto address = json.find("address");
	if (address != json.end() && address->is_object()) {
		h.road = FirstOf(*address, {"road", "pedestrian", "footway"});
		h.houseNumber = Str(*address, "house_number");
		h.postcode = Str(*address, "postcode");
		h.locality = FirstOf(*address, {"city", "town", "village", "municipality", "hamlet"});
		h.region = FirstOf(*address, {"state", "region", "county"});
		h.countryCode = Upper(Str(*address, "country_code"));
		h.country = Str(*address, "country");
		// a building that has no name of its own (a house at a position) is its address: `Naglergasse 4`, not `4`
		if (h.name.empty() && !h.road.empty() && !h.houseNumber.empty())
			h.name = h.road + " " + h.houseNumber;
		if (h.name.empty())
			h.name = Str(*address, h.addressType.c_str());
	}
	if (h.name.empty() && !h.displayName.empty())
		h.name = h.displayName.substr(0, h.displayName.find(','));
	*hit = h;
	return true;
}

std::string
Encoded(const std::string& text, const char* keep)
{
	std::string out;
	for (unsigned char c : text) {
		if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || (c != 0 && std::strchr(keep, c) != nullptr)) {
			out += (char)c;
		} else {
			char buffer[4];
			std::snprintf(buffer, sizeof(buffer), "%%%02X", c);
			out += buffer;
		}
	}
	return out;
}

}	// namespace


std::string
PercentEncode(const std::string& text)
{
	return Encoded(text, "");
}


std::string
SearchUrl(const std::string& query, const std::string& language, bool boundary, int limit, double tolerance)
{
	return std::string(kApi) + "/search?q=" + PercentEncode(query) + "&limit=" + std::to_string(limit) + Common(language, boundary, tolerance);
}


std::string
LookupUrl(const std::string& osmType, int64_t osmId, const std::string& language, bool boundary, double tolerance)
{
	const char letter = osmType.empty() ? 'N' : (char)std::toupper((unsigned char)osmType[0]);
	return std::string(kApi) + "/lookup?osm_ids=" + letter + std::to_string(osmId) + Common(language, boundary, tolerance);
}


std::string
ReverseUrl(double latitude, double longitude, const std::string& language, bool boundary, double tolerance)
{
	return std::string(kApi) + "/reverse?lat=" + Coordinate(latitude) + "&lon=" + Coordinate(longitude) + Common(language, boundary, tolerance);
}


double
BoundaryTolerance(const Hit& hit)
{
	if (!hit.hasBox)
		return kBoundaryTolerance;
	const double extent = std::max(std::fabs(hit.north - hit.south), std::fabs(hit.east - hit.west));
	return std::min(0.01, std::max(kBoundaryTolerance, extent / 200.0));
}


bool
ParseHits(const std::string& text, std::vector<Hit>* hits)
{
	Json json = Json::parse(text, nullptr, false);
	if (!json.is_array())
		return false;
	hits->clear();
	for (const Json& each : json) {
		Hit hit;
		if (ToHit(each, &hit))
			hits->push_back(hit);
	}
	return true;
}


bool
ParseHit(const std::string& text, Hit* hit)
{
	Json json = Json::parse(text, nullptr, false);
	if (!json.is_object() || json.contains("error"))
		return false;
	return ToHit(json, hit);
}


std::string
OsmUrl(const Hit& hit)
{
	if (hit.osmType.empty() || hit.osmId == 0)
		return std::string();
	return "https://www.openstreetmap.org/" + hit.osmType + "/" + std::to_string(hit.osmId);
}


std::vector<std::string>
SameAs(const Hit& hit)
{
	std::vector<std::string> links;
	if (!OsmUrl(hit).empty())
		links.push_back(OsmUrl(hit));
	if (!hit.wikidataId.empty())
		links.push_back("https://www.wikidata.org/wiki/" + hit.wikidataId);
	const size_t colon = hit.wikipedia.find(':');
	if (colon != std::string::npos && colon > 0 && colon + 1 < hit.wikipedia.size()) {
		// (a link is stored in a list that a comma separates: a comma in the title is encoded, like every mark)
		std::string title = hit.wikipedia.substr(colon + 1);
		std::replace(title.begin(), title.end(), ' ', '_');
		links.push_back("https://" + hit.wikipedia.substr(0, colon) + ".wikipedia.org/wiki/" + Encoded(title, ":/"));
	}
	return links;
}


bool
OsmFrom(const std::string& links, std::string* osmType, int64_t* osmId)
{
	const std::string host = "openstreetmap.org/";
	size_t from = 0;
	while ((from = links.find(host, from)) != std::string::npos) {
		from += host.size();
		for (const char* type : {"node", "way", "relation"}) {
			const std::string prefix = std::string(type) + "/";
			if (links.compare(from, prefix.size(), prefix) == 0) {
				const int64_t id = std::atoll(links.c_str() + from + prefix.size());
				if (id > 0) {
					*osmType = type;
					*osmId = id;
					return true;
				}
			}
		}
	}
	return false;
}


std::string
WikidataFrom(const std::string& links)
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


bool
IsAdministrative(const Hit& hit)
{
	return (hit.category == "boundary" && hit.type == "administrative") || hit.category == "place";
}


std::string
PlaceTypeIri(const Hit& hit)
{
	if (hit.category == "boundary" && hit.type == "administrative") {
		if (!hit.addressType.empty())
			return std::string(kOsmTag) + "place=" + hit.addressType;
		return std::string(kOsmTag) + "boundary=administrative";
	}
	if (hit.category.empty() || hit.type.empty())
		return std::string();
	return std::string(kOsmTag) + hit.category + "=" + hit.type;
}


std::string
AddressLine(const Hit& hit)
{
	if (hit.road.empty() && hit.postcode.empty())
		return std::string();
	std::string line;
	auto add = [&line](const std::string& part) {
		if (part.empty())
			return;
		if (!line.empty())
			line += ", ";
		line += part;
	};
	std::string street = hit.road;
	if (!street.empty() && !hit.houseNumber.empty())
		street += " " + hit.houseNumber;
	add(street);
	std::string town = hit.postcode;
	if (!hit.locality.empty())
		town += (town.empty() ? "" : " ") + hit.locality;
	add(town);
	add(hit.country);
	return line;
}


std::vector<std::string>
OtherNames(const std::string& name, const std::vector<std::string>& candidates, size_t limit)
{
	std::vector<std::string> names;
	for (const std::string& candidate : candidates) {
		if (candidate.empty() || candidate == name || names.size() >= limit)
			continue;
		if (std::find(names.begin(), names.end(), candidate) == names.end())
			names.push_back(candidate);
	}
	return names;
}


/** the words of a text, in lower case (letters and digits, the bytes of a UTF-8 letter are part of a word) */
static std::vector<std::string>
Words(const std::string& text)
{
	std::vector<std::string> words;
	std::string word;
	for (unsigned char c : text) {
		if (c >= 0x80 || std::isalnum(c)) {
			word += (char)(c >= 0x80 ? c : std::tolower(c));
		} else if (!word.empty()) {
			words.push_back(word);
			word.clear();
		}
	}
	if (!word.empty())
		words.push_back(word);
	return words;
}


bool
TitleMatches(const Hit& hit, const std::string& wanted)
{
	const std::string asked = Plain(wanted);
	if (asked.empty())
		return true;
	const std::string where = Plain(hit.displayName);
	auto fits = [&](const std::string& name) {
		const std::string plain = Plain(name);
		if (plain.empty())
			return false;
		if (plain == asked || plain.find(asked) != std::string::npos)
			return true;		// (the name is the one that was asked for, or holds it: a part of a name)
		if (asked.find(plain) == std::string::npos)
			return false;
		// the question holds the name ("Stephansdom Wien"): the rest of it must say where, i.e. be in the address of the place; "IMG 0001" does not find an office called "IMG"
		const std::vector<std::string> own = Words(name);
		for (const std::string& word : Words(wanted)) {
			bool part = false;
			for (const std::string& each : own)
				part = part || each == word;
			if (!part && where.find(word) == std::string::npos)
				return false;
		}
		return true;
	};
	if (fits(hit.name))
		return true;
	for (const std::string& other : hit.otherNames) {
		if (fits(other))
			return true;
	}
	return false;
}


float
ScoreHit(const Hit& hit, int position, int count, const std::string& wantedName)
{
	const std::string asked = Plain(wantedName);
	float name = 0.5f;		// (nothing asked)
	if (!asked.empty()) {
		// (the same as the name, or as another name of the place: the same)
		bool same = Plain(hit.name) == asked;
		for (const std::string& other : hit.otherNames)
			same = same || Plain(other) == asked;
		if (same)
			name = 1.0f;
		else if (TitleMatches(hit, wantedName))
			name = 0.6f;
		else
			name = 0.0f;
	}
	const float known = (float)std::min(1.0, std::max(0.0, hit.importance));
	const float order = count > 1 ? 1.0f - (float)position / (float)count : 1.0f;
	return 0.4f * name + 0.3f * known + 0.3f * order;
}


double
DistanceKm(const Hit& hit, const Position& position)
{
	const double kRadius = 6371.0088;
	const double kPi = 3.14159265358979323846;
	auto radians = [kPi](double degrees) { return degrees * kPi / 180.0; };
	double latitude = hit.latitude, longitude = hit.longitude;
	if (hit.hasBox) {
		// (inside the box: there; else the nearest point of the box)
		latitude = std::min(std::max(position.latitude, std::min(hit.south, hit.north)), std::max(hit.south, hit.north));
		longitude = std::min(std::max(position.longitude, std::min(hit.west, hit.east)), std::max(hit.west, hit.east));
	}
	// haversine
	const double dLatitude = radians(latitude - position.latitude);
	const double dLongitude = radians(longitude - position.longitude);
	const double a = std::sin(dLatitude / 2) * std::sin(dLatitude / 2)
		+ std::cos(radians(position.latitude)) * std::cos(radians(latitude)) * std::sin(dLongitude / 2) * std::sin(dLongitude / 2);
	return 2 * kRadius * std::asin(std::min(1.0, std::sqrt(a)));
}


std::string
DistanceText(double kilometres)
{
	char buffer[32];
	if (kilometres < 0.1)
		return "at the position";
	if (kilometres < 1.0)
		std::snprintf(buffer, sizeof(buffer), "%d m", (int)(kilometres * 1000.0 / 10.0 + 0.5) * 10);
	else if (kilometres < 10.0)
		std::snprintf(buffer, sizeof(buffer), "%.1f km", kilometres);
	else
		std::snprintf(buffer, sizeof(buffer), "%.0f km", kilometres);
	return buffer;
}


bool
LooksLikeName(const std::string& name)
{
	int letters = 0, digits = 0;
	for (unsigned char c : name) {
		if (c >= 0x80) {
			// (the bytes of a UTF-8 letter: one for each letter, counted at its first byte)
			if ((c & 0xC0) != 0x80)
				letters++;
		} else if (std::isalpha(c)) {
			letters++;
		} else if (std::isdigit(c)) {
			digits++;
		}
	}
	return letters >= 2 && letters > digits;
}


float
ScoreHitNear(const Hit& hit, int position, int count, const std::string& wantedName, const Position& near)
{
	// the parts of ScoreHit that are not the position: the name 0.4 -> 0.35, how well known 0.3 -> 0.15, the order 0.3 -> 0.15
	const std::string asked = Plain(wantedName);
	float name = 0.5f;
	if (!asked.empty()) {
		bool same = Plain(hit.name) == asked;
		for (const std::string& other : hit.otherNames)
			same = same || Plain(other) == asked;
		name = same ? 1.0f : (TitleMatches(hit, wantedName) ? 0.6f : 0.0f);
	}
	const double kilometres = DistanceKm(hit, near);
	const float closeness = (float)std::min(1.0, std::max(0.0, 1.0 - std::log10(std::max(kilometres, 1.0)) / 2.0));
	const float known = (float)std::min(1.0, std::max(0.0, hit.importance));
	const float order = count > 1 ? 1.0f - (float)position / (float)count : 1.0f;
	return 0.35f * name + 0.35f * closeness + 0.15f * known + 0.15f * order;
}

}	// namespace place
