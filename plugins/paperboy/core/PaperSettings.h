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
	/** rename the file after the pattern when it was enriched (off: a plugin does not rename files unasked) */
	bool rename = false;
	std::string renamePattern = kDefaultNamePattern;
	/** replace bibliographic attributes that already have a value (title, authors, ...); off: only fill the missing ones */
	bool overwrite = false;
	/** look a paper up by its title when no DOI is found */
	bool titleSearch = true;
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
