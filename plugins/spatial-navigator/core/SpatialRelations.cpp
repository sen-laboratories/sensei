/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpatialRelations.h"

#include <algorithm>
#include <cstdio>

namespace spatial {

namespace {

const char* const kTypePrefix = "relation/x-vnd.sen-labs.relation.spatial.";

/** whether a DE-9IM matrix fits a pattern: T is any of 0, 1, 2 (not F), * is anything */
bool
Fits(const std::string& matrix, const char* pattern)
{
	for (int i = 0; i < 9; i++) {
		const char p = pattern[i], m = matrix[i];
		if (p == '*')
			continue;
		if (p == 'T' ? m == 'F' : p != m)
			return false;
	}
	return true;
}

}	// namespace


Relation
FromDe9im(const std::string& m)
{
	// (the exteriors of two geometries always meet: a matrix where they do not is that of an empty geometry, which is related to nothing)
	if (m.size() != 9 || m[8] == 'F')
		return Relation::kNone;
	if (Fits(m, "T*F**FFF*"))
		return Relation::kEqual;
	if (Fits(m, "FF*FF****"))
		return Relation::kDisjoint;
	if (Fits(m, "FF*FT****"))
		return Relation::kTouch;
	if (Fits(m, "T*T***T**"))
		return Relation::kOverlap;
	// A inside B (the interior of A is in the interior of B, none of A is in the exterior of B): strictly inside if the borders do not meet (boundary against boundary is empty)
	if (Fits(m, "T*F**F***"))
		return m[4] == 'F' ? Relation::kWithin : Relation::kCoveredBy;
	// B inside A
	if (Fits(m, "T*****FF*"))
		return m[4] == 'F' ? Relation::kContains : Relation::kCovers;
	return Relation::kNone;
}


Relation
Inverse(Relation relation)
{
	switch (relation) {
		case Relation::kWithin:		return Relation::kContains;
		case Relation::kContains:	return Relation::kWithin;
		case Relation::kCoveredBy:	return Relation::kCovers;
		case Relation::kCovers:		return Relation::kCoveredBy;
		default:					return relation;
	}
}


std::string
TypeOf(Relation relation)
{
	switch (relation) {
		case Relation::kWithin:
		case Relation::kContains:	return std::string(kTypePrefix) + "within";
		case Relation::kCoveredBy:
		case Relation::kCovers:		return std::string(kTypePrefix) + "covered-by";
		case Relation::kTouch:		return std::string(kTypePrefix) + "touch";
		case Relation::kOverlap:	return std::string(kTypePrefix) + "overlap";
		case Relation::kEqual:		return std::string(kTypePrefix) + "equal";
		default:					return std::string();
	}
}


std::string
LabelOf(Relation relation)
{
	switch (relation) {
		case Relation::kWithin:		return "within";
		case Relation::kContains:	return "contains";
		case Relation::kCoveredBy:	return "covered by";
		case Relation::kCovers:		return "covers";
		case Relation::kTouch:		return "touches";
		case Relation::kOverlap:	return "overlaps";
		case Relation::kEqual:		return "equal to";
		default:					return std::string();
	}
}


std::string
CodeOf(Relation relation)
{
	switch (relation) {
		case Relation::kDisjoint:	return "DC";
		case Relation::kTouch:		return "EC";
		case Relation::kOverlap:	return "PO";
		case Relation::kEqual:		return "EQ";
		case Relation::kWithin:		return "NTPP";
		case Relation::kCoveredBy:	return "TPP";
		case Relation::kContains:	return "NTPPi";
		case Relation::kCovers:		return "TPPi";
		default:					return std::string();
	}
}


std::string
KindOf(Relation relation)
{
	switch (relation) {
		case Relation::kDisjoint:	return "disconnected";
		case Relation::kTouch:		return "externally connected";
		case Relation::kOverlap:	return "partially overlapping";
		case Relation::kEqual:		return "equal";
		case Relation::kWithin:		return "non-tangential proper part";
		case Relation::kCoveredBy:	return "tangential proper part";
		case Relation::kContains:	return "non-tangential proper part (inverse)";
		case Relation::kCovers:		return "tangential proper part (inverse)";
		default:					return std::string();
	}
}


bool
AgreesWithWikidata(Relation relation, bool oneInTheOther)
{
	return !oneInTheOther || (relation != Relation::kTouch && relation != Relation::kOverlap);
}


bool
BoxesMeet(const Box& a, const Box& b, double degrees)
{
	if (!a.valid || !b.valid)
		return false;
	return a.south <= b.north + degrees && b.south <= a.north + degrees && a.west <= b.east + degrees && b.west <= a.east + degrees;
}


Box
PointBox(double latitude, double longitude)
{
	Box box;
	box.south = box.north = latitude;
	box.west = box.east = longitude;
	box.valid = true;
	return box;
}


std::string
PointWkt(double latitude, double longitude)
{
	char buffer[80];
	std::snprintf(buffer, sizeof(buffer), "POINT(%.9g %.9g)", longitude, latitude);
	return buffer;
}

}	// namespace spatial
