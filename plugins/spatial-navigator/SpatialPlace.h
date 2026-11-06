/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Node.h>

#include <string>
#include <vector>

#include "SpatialRelations.h"

namespace spatial {

/** What the file of a place or location says of where it is (the attributes of the ontology SEN Spatial). */
struct PlaceData {
	entry_ref	ref;
	node_ref	node;
	std::string	name;				///< dc:title
	bool		hasPosition = false;
	double		latitude = 0;
	double		longitude = 0;
	Box			box;				///< the bounding box, else the point; not valid if the file has neither
	std::string	outline;			///< the boundary as WKT (geo:asWKT); empty if the file has none
	std::string	sameAs;				///< the links (schema:sameAs)
	std::string	wikidataId;			///< `Q5943`, from the links; empty if there is none
};

/** Reads the attributes of the file; false if it cannot be read. */
bool ReadPlace(const entry_ref& ref, PlaceData* place);

/** The WKT that stands for the place: its outline, else the point of its position; empty if it has neither. */
std::string GeometryOf(const PlaceData& place);

/** The files of all places and locations: the files of the type Place or Location on every volume that can be queried, at most @p limit. */
status_t FindPlaceFiles(std::vector<entry_ref>* refs, size_t limit = 3000);

}	// namespace spatial
