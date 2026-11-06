/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PlaceWriter.h"

#include <Node.h>
#include <TypeConstants.h>
#include <String.h>
#include <fs_attr.h>

#include <stdio.h>

#include <sen/Sen.h>
#include <sen/SenList.h>
#include <sen/SenOntoSpatial.h>

#include "../../common/ThumbnailUtil.h"
#include "ListSeparator.h"

namespace place {

namespace {

bool
HasAttr(BNode& node, const char* attribute)
{
	attr_info info;
	return node.GetAttrInfo(attribute, &info) == B_OK && info.size > 0;
}

void
PutString(BNode& node, const char* attribute, const std::string& value, bool overwrite)
{
	if (value.empty() || (!overwrite && HasAttr(node, attribute)))
		return;
	node.WriteAttr(attribute, B_STRING_TYPE, 0, value.c_str(), value.size() + 1);
}

void
PutDouble(BNode& node, const char* attribute, double value, bool overwrite)
{
	if (!overwrite && HasAttr(node, attribute))
		return;
	node.WriteAttr(attribute, B_DOUBLE_TYPE, 0, &value, sizeof(value));
}

void
PutList(BNode& node, const char* attribute, const std::vector<std::string>& values, bool overwrite)
{
	if (values.empty())
		return;
	PutString(node, attribute, sen::list::Join(values, ListSeparatorOf(sen::onto::spatial::mime::kPlace, attribute)), overwrite);
}

}	// namespace


status_t
WritePlace(const entry_ref& file, const Hit& hit, const std::string& description, bool boundary, bool overwrite)
{
	BNode node(&file);
	status_t result = node.InitCheck();
	if (result != B_OK)
		return result;

	namespace attr = sen::onto::spatial::attr;
	PutString(node, attr::kTitle, hit.name, overwrite);
	PutList(node, attr::kAlternateName, hit.otherNames, overwrite);
	PutString(node, attr::kDescription, description, overwrite);
	PutString(node, attr::kPlaceType, PlaceTypeName(hit), overwrite);
	PutString(node, attr::kPlaceTypeUri, PlaceTypeIri(hit), overwrite);
	PutString(node, attr::kUrl, hit.website, overwrite);

	// the position that the file has is its own (where a photo was taken): the place found does not replace it, whatever @p overwrite says
	PutDouble(node, attr::kLatitude, hit.latitude, false);
	PutDouble(node, attr::kLongitude, hit.longitude, false);
	if (hit.hasBox) {
		PutDouble(node, attr::kBoxSouth, hit.south, overwrite);
		PutDouble(node, attr::kBoxNorth, hit.north, overwrite);
		PutDouble(node, attr::kBoxWest, hit.west, overwrite);
		PutDouble(node, attr::kBoxEast, hit.east, overwrite);
	}
	if (boundary && !hit.wkt.empty()) {
		if (hit.wkt.size() <= kMaxBoundaryBytes)
			PutString(node, attr::kGeometry, hit.wkt, overwrite);
		else
			printf("the outline of %s is %zu bytes, too much to keep: left out.\n", hit.name.c_str(), hit.wkt.size());
	}

	PutString(node, attr::kAddress, AddressLine(hit), overwrite);
	PutString(node, attr::kPostalCode, hit.postcode, overwrite);
	PutString(node, attr::kLocality, hit.locality, overwrite);
	PutString(node, attr::kRegion, hit.region, overwrite);
	PutString(node, attr::kCountry, hit.countryCode, overwrite);

	// the links: the ones that the file has stay, the ones found are added (the link to OpenStreetMap is how the place is found again)
	const char separator = ListSeparatorOf(sen::onto::spatial::mime::kPlace, attr::kSameAs);
	std::vector<std::string> links;
	BString existing;
	if (node.ReadAttrString(attr::kSameAs, &existing) == B_OK)
		links = SplitValues(existing.String(), separator);
	for (const std::string& link : SameAs(hit)) {
		bool known = false;
		for (const std::string& each : links)
			known = known || each == link;
		if (!known)
			links.push_back(link);
	}
	const std::string joined = sen::list::Join(links, separator);
	node.WriteAttr(attr::kSameAs, B_STRING_TYPE, 0, joined.c_str(), joined.size() + 1);

	// the semantic type: what a file is for SEN (Shoji shows its form for it); a file that has one (a Location) keeps it
	if (!HasAttr(node, sen::attr::kType)) {
		const std::string type = sen::onto::spatial::mime::kPlace;
		node.WriteAttr(sen::attr::kType, B_STRING_TYPE, 0, type.c_str(), type.size() + 1);
	}
	return B_OK;
}


status_t
WriteImage(const entry_ref& file, const std::string& imageData, const Image& image, bool overwrite)
{
	BNode node(&file);
	attr_info info;
	if (!overwrite && node.GetAttrInfo(THUMBNAIL_ATTR_NAME, &info) == B_OK && info.size > 0)
		return B_OK;
	status_t result = WriteThumbnail(&file, imageData);
	if (result != B_OK)
		return result;

	// where the picture is from, and what its license asks to say
	namespace attr = sen::onto::spatial::attr;
	PutString(node, attr::kImage, image.pageUrl, true);
	PutString(node, attr::kImageLicense, image.license, true);
	PutString(node, attr::kImageCredit, CreditOf(image), true);
	return B_OK;
}

}	// namespace place
