/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Message.h>
#include <StringList.h>
#include <SupportDefs.h>

#include <string>

class BaseEnricher;

/**
 * @brief The authors of a book, all in one place.
 *
 * OpenLibrary answers a search for a book with the keys of its authors. Each of them is looked up (name, birthday, biography, photo), made a person
 * (entity/x-vnd.sen-labs.entity.person, a file next to the book, named after the author) with its attributes and its photo as the thumbnail, as for
 * the cover of the book, and related to the book: the Authorship relation, made by the SEN server. The key of the author is kept in the file
 * (OPENLIB:author_key), so that a second enrichment of the book finds the person again, takes what changed and does not make another relation.
 */
class AuthorMaker {
public:
	/**
	 * @param enricher the HTTP session and the lookup of the URLs
	 * @param overwrite replace the attributes that a person has already
	 * @param debug print what is looked up
	 */
	AuthorMaker(BaseEnricher* enricher, bool overwrite, bool debug);

	/**
	 * Makes the authors of a book. A author that cannot be looked up or related is skipped, with a message: it does not undo the others or the book.
	 * @param book the enriched book (the people are made in its folder)
	 * @param keys the keys of the authors at OpenLibrary, e.g. OL2655386A
	 * @param names their names as the search found them, in the same order (the fallback when the lookup has no name)
	 * @return the number of authors that are people with a relation to the book now
	 */
	int32			MakeAuthors(const entry_ref& book, const BStringList& keys, const BStringList& names);

private:
	status_t		Fetch(const char* key, BMessage* author);
	status_t		FetchPhoto(int64 photoId, std::string* image);
	status_t		FindOrCreatePerson(const entry_ref& book, const BString& name, const char* key, entry_ref* person, bool* created);
	void			WriteAttributes(const entry_ref& person, const BMessage& author, const BString& name, const char* key);
	status_t		Relate(const entry_ref& person, const entry_ref& book);

	BaseEnricher*	fEnricher;
	bool			fOverwrite;
	bool			fDebug;
};
