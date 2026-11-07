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

#include <geos_c.h>

#include <stdarg.h>
#include <stdio.h>

#include <algorithm>
#include <set>

#include <sen/Sen.h>
#include <sen/SenOntoSpatial.h>

#include "../../common/EntityFile.h"
#include "../../common/SocketHttp.h"
#include "SpatialWikidata.h"

namespace spatial {

namespace {

const char* const kUserAgent = "SEN-SpatialNavigator/0.1 (Haiku; https://sen-labs.org)";
// the chain of parents of a place changes rarely: kept for a month
const long kCacheSeconds = 30L * 24 * 3600;

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
Resolver::Resolve(const entry_ref& file, const std::string& relationType, std::vector<Target>* targets)
{
	PlaceData place;
	if (!ReadPlace(file, &place))
		return B_ENTRY_NOT_FOUND;
	const std::string geometry = GeometryOf(place);

	std::vector<entry_ref> candidates;
	FindPlaceFiles(&candidates);

	std::map<std::string, entry_ref> byWikidata;
	for (const entry_ref& candidate : candidates) {
		PlaceData other;
		if (!ReadPlace(candidate, &other) || other.node == place.node)
			continue;
		if (!other.wikidataId.empty())
			byWikidata[other.wikidataId] = candidate;

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
	return B_OK;
}

}	// namespace spatial
