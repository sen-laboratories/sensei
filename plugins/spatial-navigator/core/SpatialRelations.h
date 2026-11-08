/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

/**
 * The topological relations between two places (the Region Connection Calculus, RCC-8) as the spatial navigator works them out: from the DE-9IM matrix that a geometry engine (GEOS) gives for
 * two geometries, and from the bounding boxes that tell which pairs are worth asking. No Haiku and no GEOS here: the matrix is a text of nine characters, so it can be tested everywhere.
 *
 * "A is within B" means B contains A: a relation type has two labels, one for each direction, and the navigator gives the label that fits the file that the user opened the relation at.
 */
namespace spatial {

/** How place A is related to place B (RCC-8: the relation of A to B). */
enum class Relation {
	kNone,			///< not a relation (the matrix is not one of the eight)
	kDisjoint,		///< DC: no point in common. Nearly every two places are: it is never offered as a relation.
	kTouch,			///< EC: the borders meet, the places do not overlap (two neighbouring countries)
	kOverlap,		///< PO: a part in common and a part of each of their own
	kEqual,			///< EQ: the same area
	kWithin,		///< NTPP: A is inside B and does not touch its border
	kCoveredBy,		///< TPP: A is inside B and touches its border
	kContains,		///< NTPPi: B is inside A, not touching its border
	kCovers			///< TPPi: B is inside A and touches its border
};

/** The relation of A to B from the DE-9IM matrix of A relative to B (`2FF1FF212`: interior, boundary, exterior of A against interior, boundary, exterior of B). */
Relation FromDe9im(const std::string& matrix);

/** The opposite direction: A within B is B contains A; the symmetric ones are their own. */
Relation Inverse(Relation relation);

/**
 * The relation type that the relation is one of, as the ontology names it (`relation/x-vnd.sen-labs.relation.spatial.within`: within and contains are one type, covered by and covers
 * another; touch, overlap and equal are one each). Empty for kNone and kDisjoint.
 */
std::string TypeOf(Relation relation);

/** The label that the relation has seen from A, the file that the relation is opened at: `within`, `contains`, `covered by`, `covers`, `touches`, `overlaps`, `equal to`. Empty for kNone and kDisjoint. */
std::string LabelOf(Relation relation);

/** The name of the relation in the Region Connection Calculus, for the property `SEN:REL:Rcc8` of the relation: `NTPP`, `NTPPi`, `TPP`, `TPPi`, `EC`, `PO`, `EQ` (`DC` for disjoint). Empty for kNone. */
std::string CodeOf(Relation relation);

/**
 * What the code says, in words, for the property `SEN:REL:Kind` of the relation (what people read): `non-tangential proper part` (NTPP: inside, not touching the border),
 * `tangential proper part` (TPP: inside, touching the border), `externally connected` (EC: the borders meet), `partially overlapping` (PO), `equal` (EQ), `disconnected` (DC); the opposite
 * directions of the first two are `non-tangential proper part (inverse)` and `tangential proper part (inverse)`. Empty for kNone.
 */
std::string KindOf(Relation relation);

/**
 * Whether what the shapes say can stand next to what Wikidata says of the same two places. If Wikidata says that one is located in the other, the shapes can only say that it is inside
 * (within or covered by) or equal: "touch" and "overlap" are then an effect of the simplified outlines, whose borders coincide where the place reaches the limit of its parent (a district at the
 * edge of its city), and are left out. @p oneInTheOther: Wikidata says that A is located in B or B in A.
 */
bool AgreesWithWikidata(Relation relation, bool oneInTheOther);

/** The coordinates of a place that cover it: its bounding box, or a point (south = north, west = east). */
struct Box {
	double	south = 0, north = 0, west = 0, east = 0;
	bool	valid = false;
};

/** Whether two boxes meet or overlap: only places whose boxes do can touch, overlap, be equal or be inside each other (a margin of @p degrees takes care of rounded coordinates). */
bool BoxesMeet(const Box& a, const Box& b, double degrees = 1e-7);

/** The box of a point. */
Box PointBox(double latitude, double longitude);

/** The WKT of a point (`POINT(16.3731 48.2085)`: longitude first, as WKT has it). */
std::string PointWkt(double latitude, double longitude);

}	// namespace spatial
