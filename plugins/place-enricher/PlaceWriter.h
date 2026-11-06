/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>

#include <string>

#include "PlaceNominatim.h"
#include "PlaceWikidata.h"

namespace place {

/** The largest outline that is kept in an attribute (a bigger one is left out: it is for telling where places are, not for drawing a map) */
const size_t kMaxBoundaryBytes = 256 * 1024;

/**
 * Writes what Nominatim says of a place into the attributes of its file (a file of the type Place or Location, or any file that is made one): name, other names, the kind of place,
 * web site, the links where else the place is known (the link to OpenStreetMap is how the place is found again), the position, the bounding box, the outline (if @p boundary), the address,
 * the short description from Wikidata (@p description, may be empty) and, if the file has no semantic type yet, the type Place. The attributes that the file has already are kept
 * unless @p overwrite, except the position (latitude and longitude), which is never replaced: it is where the file is, and not where the place is.
 */
status_t WritePlace(const entry_ref& file, const Hit& hit, const std::string& description, bool boundary, bool overwrite);

/**
 * The picture of the place as the thumbnail of the file, with where it is from and what its license asks (the page at Commons, the license, the credit). Kept if the file has a
 * thumbnail already, unless @p overwrite.
 */
status_t WriteImage(const entry_ref& file, const std::string& imageData, const Image& image, bool overwrite);

}	// namespace place
