/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialPlace.h"

#include <Query.h>
#include <String.h>
#include <TypeConstants.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_attr.h>

#include <sen/SenOntoSpatial.h>

#include "SpatialWikidata.h"

namespace spatial {

namespace {

bool
ReadDouble(BNode& node, const char* attribute, double* value)
{
	return node.ReadAttr(attribute, B_DOUBLE_TYPE, 0, value, sizeof(*value)) == (ssize_t)sizeof(*value);
}

std::string
ReadText(BNode& node, const char* attribute)
{
	BString text;
	return node.ReadAttrString(attribute, &text) == B_OK ? std::string(text.String()) : std::string();
}

}	// namespace


bool
ReadPlace(const entry_ref& ref, PlaceData* place)
{
	BNode node(&ref);
	if (node.InitCheck() != B_OK)
		return false;
	PlaceData data;
	data.ref = ref;
	if (node.GetNodeRef(&data.node) != B_OK)
		return false;

	namespace attr = sen::onto::spatial::attr;
	data.name = ReadText(node, attr::kTitle);
	data.hasPosition = ReadDouble(node, attr::kLatitude, &data.latitude) && ReadDouble(node, attr::kLongitude, &data.longitude);
	if (ReadDouble(node, attr::kBoxSouth, &data.box.south) && ReadDouble(node, attr::kBoxNorth, &data.box.north) && ReadDouble(node, attr::kBoxWest, &data.box.west)
			&& ReadDouble(node, attr::kBoxEast, &data.box.east)) {
		data.box.valid = true;
	} else if (data.hasPosition) {
		data.box = PointBox(data.latitude, data.longitude);
	}
	data.outline = ReadText(node, attr::kGeometry);
	data.sameAs = ReadText(node, attr::kSameAs);
	data.wikidataId = WikidataIdFrom(data.sameAs);
	*place = data;
	return true;
}


std::string
GeometryOf(const PlaceData& place)
{
	if (!place.outline.empty())
		return place.outline;
	if (place.hasPosition)
		return PointWkt(place.latitude, place.longitude);
	return std::string();
}


status_t
FindPlaceFiles(std::vector<entry_ref>* refs, size_t limit)
{
	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK && refs->size() < limit) {
		if (!volume.KnowsQuery())
			continue;
		BQuery query;
		query.SetVolume(&volume);
		query.PushAttr("BEOS:TYPE");
		query.PushString(sen::onto::spatial::mime::kPlace);
		query.PushOp(B_EQ);
		query.PushAttr("BEOS:TYPE");
		query.PushString(sen::onto::spatial::mime::kLocation);
		query.PushOp(B_EQ);
		query.PushOp(B_OR);
		if (query.Fetch() != B_OK)
			continue;
		entry_ref ref;
		while (query.GetNextRef(&ref) == B_OK && refs->size() < limit)
			refs->push_back(ref);
	}
	return B_OK;
}

}	// namespace spatial
