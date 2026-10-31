/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

#include <Entry.h>
#include <Message.h>

#include "PaperApi.h"
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
	/**
	 * @param file may be renamed (settings), then it holds the new name
	 * @param overwrite replace title, authors, ... that the file has (Shift in the menu of Tracker), else only fill what is missing
	 */
	status_t					Enrich(entry_ref* file, BMessage* reply, bool overwrite);
	/** @param self contained relations are wanted: a paper has none, the reply stays empty */
	status_t					Extract(const entry_ref& file, bool self, BMessage* reply);

	const paperboy::Settings&	GetSettings() const { return fSettings; }

	/** What the last request found or why it found nothing, in a sentence for people (a notification, a reply detail). */
	const std::string&			Summary() const { return fSummary; }
	/** The headline of the notification for it: "Paper enriched", "Title from the document" or "Paper not enriched". */
	const std::string&			SummaryTitle() const { return fSummaryTitle; }

	/** Prints what is read from the beginning of the document (fields, the title-like text, the first page): what a lookup works with. */
	status_t					PrintFacts(const entry_ref& file);

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
	/** The paper that OpenAlex has under this id, for one that has no DOI (a preprint of a workshop): the same as _LookUp, without Unpaywall (it knows DOIs only). */
	status_t					_LookUpById(const std::string& openAlexId, paperboy::Paper* paper);
	/**
	 * The paper whose title is the title of the document (its DOI, if it has one: not every paper does; and its id in OpenAlex). Several papers can match (a preprint and the published paper): the best is taken,
	 * or, with the setting `choose`, the user is asked in a window (sen::ChooseCandidate).
	 * @return B_OK, B_ENTRY_NOT_FOUND if no paper matches, B_CANCELED if the user did not choose
	 */
	status_t					_FindByTitle(const DocumentFacts& facts, paperboy::Reference* paper);
	/** The works that a paper cites, with DOI and title where OpenAlex has them. */
	void						_FetchReferences(const paperboy::Paper& paper, std::vector<paperboy::Reference>* references);
	/** @return true if the file was renamed after the pattern of the settings */
	bool						_Rename(entry_ref* file, const paperboy::Paper& paper);
	/** A title that the document gives: its Title field if that looks like a title, else its biggest text on the first page. */
	std::string					_DocumentTitle(const DocumentFacts& facts) const;

	paperboy::Settings			fSettings;
	paperboy::OpenAlexAccess	fAccess;
	HttpClient					fHttp;
	std::string					fSummary;
	std::string					fSummaryTitle;
};
