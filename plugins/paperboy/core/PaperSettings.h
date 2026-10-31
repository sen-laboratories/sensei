/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

#include "PaperName.h"

namespace paperboy {

/**
 * The settings of paperboy. A plugin has no window, so they are a text file of `key = value` lines
 * (`~/config/settings/sen/paperboy.settings`, `#` starts a comment); the command line and the environment override them.
 */
struct Settings {
	/** Unpaywall and OpenAlex ask for an address in their "polite pool"; Unpaywall refuses requests without one. */
	std::string email;
	/** ask Unpaywall for the open access status (needs an address), else the data of OpenAlex is used */
	bool unpaywall = true;
	/** The key of OpenAlex (free, https://openalex.org/settings/api): without one all requests of an address share a small daily budget. */
	std::string apiKey;
	/** rename the file after the title once it was enriched: papers come with names that say nothing (the numbers of a conference) */
	bool rename = true;
	std::string renamePattern = kDefaultNamePattern;
	/** blanks in the new name become `_` (`Deep_learning.pdf`) */
	bool renameUnderscores = false;
	/** replace bibliographic attributes that already have a value (title, authors, ...); off: only fill the missing ones */
	bool overwrite = false;
	/** look a paper up by its title when no DOI is found */
	bool titleSearch = true;
	/** if the title search finds several papers (a preprint and the published one, ...): ask which one; off: take the best (`--auto`) */
	bool choose = true;
	/**
	 * The other sources, asked in this order when OpenAlex does not know a paper: Semantic Scholar, DBLP and arXiv (none of them needs a key). A paper whose document
	 * names an arXiv identifier is looked up at arXiv first, whatever the settings say about the title search.
	 */
	bool semanticScholar = true;
	bool dblp = true;
	bool arxiv = true;
	/**
	 * The authors of an enriched paper become people (files next to the paper, with the Authorship relation): "all" of the first `maxAuthors`, "orcid" only those that
	 * OpenAlex gives an ORCID iD (they are then looked up at ORCID and Wikidata: biography, homepage, employer and a photo), "no" none.
	 */
	std::string authors = "all";
	int maxAuthors = 10;
	/** the pages (of the beginning) that are searched for a DOI */
	int maxPages = 3;
	/** extraction: report only the citations whose paper is on this computer */
	bool localOnly = false;
};

/** @brief Reads `key = value` lines into the settings; unknown keys and bad values are ignored (the defaults stay). */
void ParseSettings(const std::string& text, Settings* settings);

/** @brief true for "yes", "true", "on" and "1" (any case), false for "no", "false", "off", "0"; else @p fallback. */
bool ParseBool(const std::string& value, bool fallback);

/** @brief Whether a string can be an address of e-mail (an @ between two parts, a dot in the domain). */
bool LooksLikeEmail(const std::string& text);

}	// namespace paperboy
