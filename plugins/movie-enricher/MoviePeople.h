/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>

#include <string>
#include <vector>

#include "MovieTmdb.h"

namespace movie {

/** Where the people come from: TMDB in the plugin, a stand-in in the tests. */
class PersonSource {
public:
	virtual					~PersonSource() {}
	/** the details of a person; false if they cannot be had (the person is skipped) */
	virtual bool			Fetch(int64_t id, PersonDetails* person) = 0;
	/** the photo of a person (the path that TMDB gives): the image, empty if there is none */
	virtual bool			FetchPhoto(const std::string& path, std::string* image) = 0;
};

struct PeopleResult {
	int		people = 0;			///< the people that are files now (made or found)
	int		made = 0;			///< of them made new
	int		contributions = 0;	///< the relations that are there now
	int		skipped = 0;		///< people that could not be made
};

/**
 * The people of the credits of a movie: each is a person (a file of the type entity/x-vnd.sen-labs.entity.person in the Library of the current context, see
 * sensei::FindOrCreateEntityFile) with the name, the birthday, the biography, the links where else the person is known (TMDB, IMDb, Wikidata) and the photo as its thumbnail, and
 * has a Contribution (the relation type of the core ontology) to the movie for every credit: the role (Director, Writer, Producer, Composer, Cinematographer, Editor, Actor), for
 * an actor the character, and the place in the credits. A person with several credits is one file with several Contributions. A person that is there already (found by the TMDB link)
 * is updated in place and not made again.
 * @param overwrite replace what a person already has
 */
PeopleResult MakePeople(const entry_ref& movie, const std::vector<Credit>& credits, PersonSource* source, bool overwrite, bool debug);

}	// namespace movie
