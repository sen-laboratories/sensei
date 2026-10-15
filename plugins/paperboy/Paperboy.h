/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

#include <Entry.h>
#include <Message.h>

#include "PaperHttp.h"
#include "PaperModel.h"
#include "PaperSettings.h"
#include "PaperText.h"

/**
 * What the plugin does with a paper, in the three features of SENSEI:
 *
 *  - identify: is this a scientific paper? (a DOI in the document says so) sets the semantic type, no network
 *  - enrich:   look the paper up (OpenAlex, Unpaywall) and write the metadata, the open access links and the citations into the attributes
 *  - extract:  the papers that this one cites, as relations (from the attributes if enriched, else asked from OpenAlex)
 */
class Paperboy {
public:
								Paperboy(const paperboy::Settings& settings);

	status_t					Identify(const entry_ref& file, BMessage* reply);
	/** @param file may be renamed (settings), then it holds the new name */
	status_t					Enrich(entry_ref* file, BMessage* reply);
	/** @param self contained relations are wanted: a paper has none, the reply stays empty */
	status_t					Extract(const entry_ref& file, bool self, BMessage* reply);

	const paperboy::Settings&	GetSettings() const { return fSettings; }

private:
	/** a DOI and where it was found, see paperboy::source */
	struct Candidate {
		std::string				doi;
		const char*				source;
	};

	/** The DOIs that a file may have, the most trusted first. A DOI stored by an earlier run is the only one. */
	std::vector<Candidate>		_Candidates(const entry_ref& file, DocumentFacts* facts, bool* factsRead);
	/** @return B_OK, B_ENTRY_NOT_FOUND if OpenAlex does not know the DOI, another error if the service could not be asked */
	status_t					_LookUp(const std::string& doi, paperboy::Paper* paper, bool withAccess);
	/** The DOI of the paper whose title is the title of the document (if there is one that matches). */
	std::string					_FindByTitle(const DocumentFacts& facts);
	/** The works that a paper cites, with DOI and title where OpenAlex has them. */
	void						_FetchReferences(const paperboy::Paper& paper, std::vector<paperboy::Reference>* references);
	/** @return the new name of the file if it was renamed after the pattern of the settings */
	bool						_Rename(entry_ref* file, const paperboy::Paper& paper);

	paperboy::Settings			fSettings;
	HttpClient					fHttp;
};
