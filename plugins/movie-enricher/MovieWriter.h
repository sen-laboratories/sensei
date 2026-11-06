/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>

#include <string>

#include "MovieTmdb.h"

namespace movie {

/**
 * Writes what TMDB says of a movie into the attributes of its file (the video, or a file of the type Movie): title, original title, date of the first release, length (ISO 8601),
 * description, genres, languages, countries, the links where else the movie is known (the TMDB link is how the movie is found again) and, if the file has no semantic type yet, the type
 * Movie. The attributes that the file has already are kept unless @p overwrite. The poster is the thumbnail (WritePoster()).
 */
status_t WriteMovie(const entry_ref& file, const Details& details, bool overwrite);

/**
 * The names of the director and the actors as attributes of the file (the settings say that entities are not made as files, see sen::plugins::ExtractEntitiesIntoFiles()): from the
 * credits, in the order of their position. Kept if the file has them, unless @p overwrite.
 */
status_t WriteNames(const entry_ref& file, const std::vector<Credit>& credits, bool overwrite);

/** The poster as the thumbnail of the file; kept if the file has a thumbnail already, unless @p overwrite. */
status_t WritePoster(const entry_ref& file, const std::string& image, bool overwrite);

}	// namespace movie
