/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

namespace movie {

/**
 * The settings of the movie enricher: lines of `key = value` in `~/config/settings/sen/movies.settings` (`#` starts a comment), changed in the settings window of SEN. The
 * options of the command line win over them for one run. Plain C++.
 */
struct Settings {
	/** The API key (v3) of The Movie Database: free, https://www.themoviedb.org/settings/api. Without one nothing can be looked up. */
	std::string apiKey;
	/** The language of the titles, descriptions and biographies that TMDB gives: `en-US`, `de-DE`, ... */
	std::string language = "en-US";
	/** if several movies are found: ask which one is meant; off: the best one is taken (`--auto`) */
	bool choose = true;
	/** make people of the director and the key crew (writers, producers, composer, cinematography, editing), with a Contribution to the movie */
	bool crew = true;
	/** make people of the cast, with a Contribution (the role Actor, the character and the place in the credits) */
	bool cast = true;
	/** the most actors that are made */
	int maxCast = 10;
};

/** Reads `key = value` lines into the settings; unknown keys and bad values are ignored (what is there stays). */
void ParseSettings(const std::string& text, Settings* settings);

/** Whether a value can be the value of a setting (an empty key clears it), so that the settings window can say what is wrong; `reason` gets it. */
bool ValidateSetting(const std::string& key, const std::string& value, std::string* reason = nullptr);

}	// namespace movie
