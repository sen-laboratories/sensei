/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialResolver.h"

#include <FindDirectory.h>
#include <Node.h>
#include <Path.h>
#include <String.h>
#include <TypeConstants.h>
#include <fs_attr.h>

#include <OS.h>
#include <Roster.h>

#include <geos_c.h>

#include <stdarg.h>
#include <stdio.h>

#include <algorithm>
#include <set>

#include <sen/Sen.h>
#include <sen/SenOntoCore.h>
#include <sen/SenOntoSpatial.h>
#include <sen/SenPluginSettings.h>

#include "../../common/EntityFile.h"
#include "../../common/Notice.h"
#include "../../common/SocketHttp.h"
#include "SpatialSettingsForm.h"
#include "SpatialWikidata.h"

namespace spatial {

namespace {

const char* const kUserAgent = "SEN-SpatialNavigator/0.1 (Haiku; https://sen-labs.org)";
// the chain of parents of a place changes rarely: kept for a month
const long kCacheSeconds = 30L * 24 * 3600;
// what is around a place changes now and then (a shop closes), but the map is not remapped that often: kept for a day (setting poi_cache_hours)
// the places that are made for the things nearby and are enriched (their picture, from Wikidata) at once: only those with a Wikidata item, and not more than this per question
const size_t kMaxPoiPictures = 12;
// all of the time that Overpass has for one question, in microseconds
const bigtime_t kPoiLimit = 80 * 1000000LL;

void
GeosMessage(const char* format, ...)
{
	// (GEOS says what is wrong with a geometry: that is no news for the user, the pair is left out)
	va_list arguments;
	va_start(arguments, format);
	va_end(arguments);
}

void
PutString(BNode& node, const char* attribute, const std::string& value)
{
	if (!value.empty())
		node.WriteAttr(attribute, B_STRING_TYPE, 0, value.c_str(), value.size() + 1);
}

void
PutDouble(BNode& node, const char* attribute, double value)
{
	node.WriteAttr(attribute, B_DOUBLE_TYPE, 0, &value, sizeof(value));
}

}	// namespace


Resolver::Resolver(const std::string& language, bool debug, const BMessage* libraries)
	:
	fLanguage(language),
	fDebug(debug),
	fCache(NULL),
	fContext(NULL)
{
	LoadSettings(&fSettings);
	BPath folder;
	if (find_directory(B_USER_CACHE_DIRECTORY, &folder, true) == B_OK && folder.Append("sen/spatial-navigator") == B_OK)
		fCache = new sensei::DiskCache(folder.Path());
	if (libraries != NULL && libraries->FindRef(sen::onto::spatial::mime::kPlace, &fLibrary) == B_OK)
		fHasLibrary = true;
	GEOSContextHandle_t context = GEOS_init_r();
	GEOSContext_setNoticeHandler_r(context, GeosMessage);
	GEOSContext_setErrorHandler_r(context, GeosMessage);
	fContext = context;
}


Resolver::~Resolver()
{
	GEOS_finish_r((GEOSContextHandle_t)fContext);
	delete fCache;
}


std::string
Resolver::_Matrix(const std::string& wktA, const std::string& wktB)
{
	GEOSContextHandle_t context = (GEOSContextHandle_t)fContext;
	GEOSWKTReader* reader = GEOSWKTReader_create_r(context);
	GEOSGeometry* a = GEOSWKTReader_read_r(context, reader, wktA.c_str());
	GEOSGeometry* b = GEOSWKTReader_read_r(context, reader, wktB.c_str());
	GEOSWKTReader_destroy_r(context, reader);

	std::string matrix;
	if (a != NULL && b != NULL) {
		char* text = GEOSRelate_r(context, a, b);
		if (text == NULL) {
			// (a simplified outline can cross itself: made valid, it can be compared)
			GEOSGeometry* validA = GEOSMakeValid_r(context, a);
			GEOSGeometry* validB = GEOSMakeValid_r(context, b);
			if (validA != NULL && validB != NULL)
				text = GEOSRelate_r(context, validA, validB);
			if (validA != NULL)
				GEOSGeom_destroy_r(context, validA);
			if (validB != NULL)
				GEOSGeom_destroy_r(context, validB);
		}
		if (text != NULL) {
			matrix = text;
			GEOSFree_r(context, text);
		}
	}
	if (a != NULL)
		GEOSGeom_destroy_r(context, a);
	if (b != NULL)
		GEOSGeom_destroy_r(context, b);
	return matrix;
}


status_t
Resolver::_MakePlace(const entry_ref& beside, const Ancestor& ancestor, entry_ref* made)
{
	const std::string link = "https://www.wikidata.org/wiki/" + ancestor.id;
	bool created = false;
	status_t result = sensei::FindOrCreateEntityFile(beside, sen::onto::spatial::mime::kPlace, ancestor.label.c_str(), ancestor.id.c_str(),
		[&link](BNode& node) {
			// the place that is meant: the one with this link, or one of that name that has no link at all (a place that was made by hand)
			BString links;
			node.ReadAttrString(sen::onto::spatial::attr::kSameAs, &links);
			return links.IsEmpty() || links.FindFirst(link.c_str()) >= 0;
		}, made, &created, fHasLibrary ? &fLibrary : NULL);
	if (result != B_OK)
		return result;
	if (created) {
		BNode node(made);
		namespace attr = sen::onto::spatial::attr;
		PutString(node, attr::kTitle, ancestor.label);
		PutString(node, attr::kSameAs, link);
		PutString(node, attr::kDescription, ancestor.description);
		PutString(node, attr::kPlaceType, ancestor.kind);			// (the kind in a word, and as the class at Wikidata)
		PutString(node, attr::kPlaceTypeUri, ancestor.kindUri);
		if (ancestor.hasPosition) {
			PutDouble(node, attr::kLatitude, ancestor.latitude);
			PutDouble(node, attr::kLongitude, ancestor.longitude);
		}
		PutString(node, sen::attr::kType, sen::onto::spatial::mime::kPlace);
		fMade.push_back(*made);
	}
	return B_OK;
}


bool
Resolver::_Ancestors(const std::string& wikidataId, std::vector<Ancestor>* ancestors)
{
	auto known = fAncestors.find(wikidataId);
	if (known != fAncestors.end()) {
		*ancestors = known->second;
		return true;
	}
	const std::string url = AncestorsUrl(wikidataId, fLanguage);
	std::string body;
	if (fCache == NULL || !fCache->Get(url, &body, kCacheSeconds)) {
		// (the query service answers 502 or 503 now and then: asked once more after a moment)
		int status = 0;
		status_t result = B_ERROR;
		for (int attempt = 0; attempt < 2 && result != B_OK; attempt++) {
			if (attempt > 0)
				snooze(2000000);
			result = sensei::SocketGet(url, kUserAgent, "application/sparql-results+json", &body, &status);
		}
		if (result != B_OK) {
			printf("could not ask Wikidata for the places that contain %s (status %d)\n", wikidataId.c_str(), status);
			return false;
		}
		if (fCache != NULL)
			fCache->Put(url, body);
	}
	if (!ParseAncestors(body, ancestors)) {
		printf("the answer of Wikidata for %s is not one\n", wikidataId.c_str());
		return false;
	}
	fAncestors[wikidataId] = *ancestors;
	return true;
}


bool
Resolver::_WikidataContains(const std::string& a, const std::string& b)
{
	std::vector<Ancestor> ancestors;
	for (const auto& pair : {std::make_pair(a, b), std::make_pair(b, a)}) {
		if (!_Ancestors(pair.first, &ancestors))
			continue;
		for (const Ancestor& each : ancestors) {
			if (each.id == pair.second)
				return true;
		}
	}
	return false;
}


void
Resolver::_AddAncestors(const PlaceData& place, const std::map<std::string, entry_ref>& byWikidata, std::vector<Target>* targets)
{
	if (place.wikidataId.empty())
		return;
	std::vector<Ancestor> ancestors;
	if (!_Ancestors(place.wikidataId, &ancestors))
		return;

	for (const Ancestor& ancestor : ancestors) {
		entry_ref ref;
		auto known = byWikidata.find(ancestor.id);
		if (known != byWikidata.end()) {
			ref = known->second;
		} else if (!ancestor.label.empty()) {
			if (_MakePlace(place.ref, ancestor, &ref) != B_OK) {
				printf("could not make a place for %s (%s)\n", ancestor.label.c_str(), ancestor.id.c_str());
				continue;
			}
			if (fDebug)
				printf("made %s (%s)\n", ancestor.label.c_str(), ancestor.id.c_str());
		} else {
			continue;
		}
		bool present = false;
		for (const Target& each : *targets)
			present = present || each.ref == ref;
		if (present || ref == place.ref)
			continue;
		Target target;
		target.ref = ref;
		target.relation = Relation::kWithin;
		target.fromWikidata = true;
		targets->push_back(target);
	}
}


status_t
Resolver::_MakePoi(const entry_ref& beside, const Poi& poi, entry_ref* made)
{
	const std::string link = OsmUrl(poi);
	bool created = false;
	// (a name that many have - Billa, a bus stop - is told apart by its address, else by the number at OpenStreetMap; up to a hundred are made at once: not put into the sticky context,
	// which would be a hundred messages to the server that waits for this plugin)
	auto isThis = [&link](BNode& node) {
		BString links;
		node.ReadAttrString(sen::onto::spatial::attr::kSameAs, &links);
		return links.FindFirst(link.c_str()) >= 0;
	};
	status_t result = sensei::FindOrCreateEntityFile(beside, sen::onto::spatial::mime::kPlace, poi.name.c_str(), poi.address.empty() ? std::to_string(poi.osmId).c_str() : poi.address.c_str(),
		isThis, made, &created, fHasLibrary ? &fLibrary : NULL, false);
	if (result == B_NAME_IN_USE && !poi.address.empty()) {
		result = sensei::FindOrCreateEntityFile(beside, sen::onto::spatial::mime::kPlace, poi.name.c_str(), std::to_string(poi.osmId).c_str(),
			isThis, made, &created, fHasLibrary ? &fLibrary : NULL, false);
	}
	if (result != B_OK)
		return result;
	if (created) {
		BNode node(made);
		namespace attr = sen::onto::spatial::attr;
		PutString(node, attr::kTitle, poi.name);
		PutString(node, attr::kDescription, poi.description);
		std::string otherNames;
		for (const std::string& other : poi.alternateNames) {
			if (other.find(',') == std::string::npos)
				otherNames += (otherNames.empty() ? "" : ",") + other;
		}
		PutString(node, attr::kAlternateName, otherNames);
		std::string links = link;
		if (!poi.wikidataId.empty())
			links += ",https://www.wikidata.org/wiki/" + poi.wikidataId;
		PutString(node, attr::kSameAs, links);
		PutString(node, attr::kPlaceType, poi.kind);
		PutString(node, attr::kPlaceTypeUri, poi.kindUri);
		PutString(node, attr::kUrl, poi.website);
		PutString(node, attr::kAddress, poi.address);
		PutString(node, attr::kPostalCode, poi.postcode);
		PutString(node, attr::kLocality, poi.locality);
		PutDouble(node, attr::kLatitude, poi.latitude);
		PutDouble(node, attr::kLongitude, poi.longitude);
		PutString(node, sen::attr::kType, sen::onto::spatial::mime::kPlace);
		fMadePois++;
		// a thing with a Wikidata item can have a picture (a photo, the coat of arms): SUESS finds it in the background, for the first few
		if (!poi.wikidataId.empty() && fMadePictures < kMaxPoiPictures) {
			fMade.push_back(*made);
			fMadePictures++;
		}
	}
	return B_OK;
}


bool
Resolver::_Pois(const Box& area, std::vector<Poi>* pois)
{
	// (the answer is kept under the question of the public server, whichever server answered: the data are the same)
	const std::string url = PoisUrl(area);
	// the server of the settings, and its key: sent as a bearer token, or in the address; not over a connection that is not secure
	const std::string key = OverpassKey(fSettings);
	std::string endpoint = WithKey(OverpassEndpoint(fSettings), key);
	std::string authorization;
	if (!key.empty() && OverpassEndpoint(fSettings).find("{key}") == std::string::npos) {
		if (endpoint.compare(0, 8, "https://") == 0)
			authorization = "Bearer " + key;
		else
			printf("the key of the Overpass server is not sent: the address is not https\n");
	}
	const std::string request = PoisUrl(area, endpoint);
	std::string body;
	if (fCache == NULL || !fCache->Get(url, &body, (long) fSettings.poiCacheHours * 3600L)) {
		if (fDebug)
			printf("asking %s\n", OverpassEndpoint(fSettings).c_str());		// (not the address with the key)
		// (the public Overpass server works out an answer for half a minute when it is busy; the SEN server waits 90 seconds for a navigator. One that fails at once - 429, 502 when
		// it is busy - is asked once more after a moment, if there is time)
		int status = 0;
		status_t result = B_ERROR;
		const bigtime_t started = system_time();
		for (int attempt = 0; attempt < 2 && result != B_OK; attempt++) {
			if (attempt > 0) {
				if (system_time() - started > 15 * 1000000LL)
					break;
				snooze(3000000);
			}
			const bigtime_t left = kPoiLimit - (system_time() - started);
			result = sensei::SocketGet(request, kUserAgent, "application/json", &body, &status, left, authorization);
		}
		if (result != B_OK) {
			printf("could not ask Overpass (%s) for what is nearby (status %d)\n", OverpassEndpoint(fSettings).c_str(), status);
			return false;
		}
		// (kept only if it is an answer: a timeout is one with a remark and no elements)
		if (!ParsePois(body, fLanguage, pois)) {
			printf("the answer of Overpass is not one for what is nearby\n");
			return false;
		}
		if (fCache != NULL)
			fCache->Put(url, body);
		return true;
	}
	return ParsePois(body, fLanguage, pois);
}


status_t
Resolver::_AddPointsOfInterest(const PlaceData& place, const std::map<std::string, entry_ref>& byOsm, std::vector<Target>* targets)
{
	const Box area = PoiBox(place.box, 2.0 * fSettings.poiRadius / 1000.0);
	if (!area.valid) {
		printf("%s: %s\n", place.name.c_str(), place.box.valid ? "too large for what is nearby (more than 3 km across)" : "no position");
		return B_OK;
	}
	std::vector<Poi> found;
	if (!_Pois(area, &found))
		return B_ERROR;		// (Overpass could not be asked: the user is told, an empty list would say that there is nothing)
	// the notable ones and the nearest first, from the middle of the place, as many as the settings say
	const double middleLatitude = (place.box.south + place.box.north) / 2;
	const double middleLongitude = (place.box.west + place.box.east) / 2;
	const std::vector<Poi> pois = Rank(found, middleLatitude, middleLongitude, (size_t)fSettings.poiLimit + 1);

	const std::string own = OsmKeyFrom(place.sameAs);
	size_t added = 0;
	for (const Poi& poi : pois) {
		if (added >= (size_t)fSettings.poiLimit)
			break;
		if (OsmKey(poi) == own)
			continue;
		entry_ref ref;
		auto known = byOsm.find(OsmKey(poi));
		if (known != byOsm.end()) {
			ref = known->second;
		} else if (_MakePoi(place.ref, poi, &ref) != B_OK) {
			printf("could not make a place for %s (%s)\n", poi.name.c_str(), OsmKey(poi).c_str());
			continue;
		}
		if (ref == place.ref)
			continue;
		Target target;
		target.ref = ref;
		target.nearby = true;
		target.label = LabelFor(poi);
		target.distanceMetres = (int)(DistanceKm(middleLatitude, middleLongitude, poi.latitude, poi.longitude) * 1000 + 0.5);
		targets->push_back(target);
		added++;
	}
	if (fDebug)
		printf("%s: %zu of %zu named things in the area, %d made\n", place.name.c_str(), added, found.size(), fMadePois);
	return B_OK;
}


// SUESS, the place enricher: it finds the position of a place from its name and address
static const char* const kPlaceEnricherSignature = "application/x-vnd.sen-plugin.sen-labs.place-enricher";
// the longest that SUESS is waited for (a lookup of an address and of the Wikidata item); the server waits 90 seconds for this plugin altogether
static const bigtime_t kEnrichWait = 40 * 1000000LL;

bool
Resolver::_NeedsPosition(const entry_ref& file, PlaceData* place)
{
	if (!sen::plugins::EnrichOnDemand()) {
		sensei::Notify("SEN Spatial Navigator", "This place has no position yet",
			(std::string(place->name.empty() ? file.name : place->name.c_str()) + " has no position, so there is nothing near it to find. Enrich it first (SUESS looks up its address); "
				"or turn on \"Enrich automatically on demand\" in the settings of SEN, under Plugins > Enrich.").c_str(), false, &file);
		printf("%s: no position (the setting Enrich automatically on demand is off)\n", place->name.c_str());
		return false;
	}

	entry_ref plugin;
	if (be_roster->FindApp(kPlaceEnricherSignature, &plugin) != B_OK) {
		sensei::Notify("SEN Spatial Navigator", "This place has no position yet",
			(std::string(place->name.empty() ? file.name : place->name.c_str()) + " has no position, and SUESS, the place enricher, is not installed to look it up.").c_str(), false, &file);
		return false;
	}

	// (the plugin is started as the server starts it, with the file; it asks nothing of the user, and it says by a notification what it found)
	BMessage launch(B_REFS_RECEIVED);
	launch.AddRef("refs", &file);
	launch.AddBool("wipe", false);
	launch.AddBool("auto", true);
	team_id team = -1;
	status_t result = be_roster->Launch(&plugin, &launch, &team);
	if (result != B_OK && result != B_ALREADY_RUNNING) {
		printf("%s: could not start SUESS: %s\n", place->name.c_str(), strerror(result));
		return false;
	}
	const bigtime_t deadline = system_time() + kEnrichWait;
	app_info info;
	while (team >= 0 && be_roster->GetRunningAppInfo(team, &info) == B_OK && system_time() < deadline)
		snooze(200000);

	if (!ReadPlace(file, place))
		return false;
	if (!place->box.valid) {
		printf("%s: SUESS found no position\n", place->name.c_str());
		sensei::Notify("SEN Spatial Navigator", "No position found",
			(std::string("SUESS found no position for ") + (place->name.empty() ? file.name : place->name.c_str()) + ". Check its name and address.").c_str(), false, &file);
		return false;
	}
	return true;
}


status_t
Resolver::Resolve(const entry_ref& file, const std::string& relationType, std::vector<Target>* targets)
{
	PlaceData place;
	if (!ReadPlace(file, &place))
		return B_ENTRY_NOT_FOUND;

	// A relation starts from a position. A place that has none (an address only: a place that was made by hand) is enriched first, if the settings say so, else the user is told: an empty
	// list would say that there is nothing, and nothing that there is a reason. (The places of Wikidata need no position: `within` goes by the link.)
	const bool pointsOfInterest = relationType == sen::onto::spatial::mime::kNearby;
	const bool hasStart = place.box.valid || !place.outline.empty()
		|| (!pointsOfInterest && !place.wikidataId.empty());
	if (!hasStart && !_NeedsPosition(file, &place))
		return B_OK;
	const std::string geometry = GeometryOf(place);

	std::vector<entry_ref> candidates;
	FindPlaceFiles(&candidates);

	std::map<std::string, entry_ref> byWikidata;
	std::map<std::string, entry_ref> byOsm;
	for (const entry_ref& candidate : candidates) {
		PlaceData other;
		if (!ReadPlace(candidate, &other) || other.node == place.node)
			continue;
		if (!other.wikidataId.empty())
			byWikidata[other.wikidataId] = candidate;
		if (pointsOfInterest) {
			const std::string key = OsmKeyFrom(other.sameAs);
			if (!key.empty())
				byOsm[key] = candidate;
			continue;		// (no outlines to compare for this relation)
		}

		// only the places whose boxes meet can be related at all (the others are disjoint): the geometry engine is asked about those
		if (geometry.empty() || !BoxesMeet(place.box, other.box))
			continue;
		const std::string otherGeometry = GeometryOf(other);
		if (otherGeometry.empty())
			continue;
		const Relation relation = FromDe9im(_Matrix(geometry, otherGeometry));
		if (relation == Relation::kNone || relation == Relation::kDisjoint || TypeOf(relation) != relationType)
			continue;
		// what Wikidata says wins over simplified outlines that cross or meet where a place reaches the limit of its parent
		if ((relation == Relation::kOverlap || relation == Relation::kTouch) && !place.wikidataId.empty() && !other.wikidataId.empty()
				&& !AgreesWithWikidata(relation, _WikidataContains(place.wikidataId, other.wikidataId))) {
			if (fDebug)
				printf("%s %s %s (%s): left out, Wikidata says that one is located in the other\n", place.name.c_str(), LabelOf(relation).c_str(), other.name.c_str(), CodeOf(relation).c_str());
			continue;
		}
		if (fDebug)
			printf("%s %s %s (%s)\n", place.name.c_str(), LabelOf(relation).c_str(), other.name.c_str(), CodeOf(relation).c_str());
		Target target;
		target.ref = candidate;
		target.relation = relation;
		targets->push_back(target);
	}

	// the places that contain this one, from Wikidata: also those that have no outline or are not on this computer yet
	if (relationType == TypeOf(Relation::kWithin))
		_AddAncestors(place, byWikidata, targets);
	else if (pointsOfInterest)
		return _AddPointsOfInterest(place, byOsm, targets);
	return B_OK;
}

}	// namespace spatial
