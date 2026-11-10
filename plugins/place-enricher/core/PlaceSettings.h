/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

namespace place {

/**
 * The settings of Spaces, the place enricher: lines of `key = value` in `~/config/settings/sen/spaces.settings` (`#` starts a comment), changed in the settings window of SEN. The options of
 * the command line win over them for one run. Plain C++.
 */
struct Settings {
	/** The language of the names and descriptions: `de`, `en`, `de-AT`...; empty: the language of the system (the application asks the system), else what the place has locally. */
	std::string language;
	/**
	 * Look up a file that is NOT a place by its position: an ordinary file (a photo from a camera) that has a position (schema:latitude, schema:longitude) and no title and no name of a
	 * place. OFF by default: the position of the user's file goes to a third party (the Nominatim service of OpenStreetMap). A Place or Location file that has a position and no name is
	 * always looked up by it (the position is the place's own, not where the user was), and a position that only ranks the answers of a search by name is never sent.
	 */
	bool lookupByPosition = false;
	/**
	 * Look for the places that are near the user first: the application asks Basho (the location server) where the computer is, roughly (the city, as far as the user allows it there), ranks
	 * the answers by their distance from it, and sends the area around it (a box of some tens of kilometres, not the position) to OpenStreetMap as the area to search first. For a name that
	 * says nothing of where (a station called "Westbahnhof"), a file with no position of its own and no city. Nothing happens if Basho is not there or the user does not allow it.
	 */
	bool nearMe = true;
	/** a picture of the place as the thumbnail: the photo, coat of arms, flag or map that Wikidata names, with its credit (two more requests, to Wikidata and Wikimedia Commons) */
	bool picture = true;
	/** keep the outline of the place (simplified) as `geo:asWKT`: what tells where places are in relation to each other (within, touches, overlaps...) */
	bool boundary = true;
	/** if several places are found: ask which one is meant; off: the best one is taken (`--auto`). Not a setting of the file: the settings of SEN that all plugins share say it. */
	bool choose = true;
};

/** Reads `key = value` lines into the settings; unknown keys and bad values are ignored (what is there stays). */
void ParseSettings(const std::string& text, Settings* settings);

/** Whether a value can be the value of a setting (an empty language is the system's), so that the settings window can say what is wrong; `reason` gets it. */
bool ValidateSetting(const std::string& key, const std::string& value, std::string* reason = nullptr);

}	// namespace place
