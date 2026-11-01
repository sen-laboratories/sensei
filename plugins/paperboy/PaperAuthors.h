/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <SupportDefs.h>

#include <string>

#include "PaperModel.h"
#include "PaperPeople.h"
#include "PaperSettings.h"

class HttpClient;

/**
 * @brief The authors of a paper, as people: files next to the paper, with the Authorship relation to it.
 *
 * What OpenAlex says about each authorship (the name, the ORCID iD) is the start. An author with an ORCID iD is looked up at ORCID (name, biography,
 * homepage, current employer) and, by the same iD, at Wikidata, which for a notable person has a photo at Wikimedia Commons: the photo is the thumbnail of
 * the file (as the cover is for a book), its license and photographer are attributes of the person, too. An author without an iD is a person with the name
 * alone: nobody is guessed from a name.
 *
 * A person that exists is found again (by the ORCID iD in `schema:sameAs`, anywhere on the volume; else by the name, in the folder of the paper) and not made twice;
 * what the person has written stays, unless the enrichment overwrites.
 */
class AuthorMaker {
public:
	AuthorMaker(HttpClient* http, const paperboy::Settings& settings, bool overwrite);

	/**
	 * Makes the authors of a paper. An author that cannot be made or related is skipped, with a message: it does not undo the others or the paper.
	 * @param file the enriched paper (the people are made in its folder)
	 * @return the number of authors that are people with a relation to the paper now
	 */
	int32			MakeAuthors(const entry_ref& file, const paperboy::Paper& paper);

private:
	/** the data of the person from ORCID, Wikidata and Commons, as far as they know it; false if none of them did */
	bool			_Lookup(paperboy::PersonData* person, std::string* photo);
	status_t		_FindOrCreate(const entry_ref& paper, const paperboy::PersonData& person, entry_ref* found, bool* created);
	bool			_FindByOrcid(const std::string& orcid, const entry_ref& near, entry_ref* found);
	void			_WriteAttributes(const entry_ref& person, const paperboy::PersonData& data);
	status_t		_Relate(const entry_ref& person, const entry_ref& paper);

	HttpClient*		fHttp;
	paperboy::Settings fSettings;
	bool			fOverwrite;
};
