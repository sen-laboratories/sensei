/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

namespace bert {

/**
 * The settings of BERT: lines of `key = value` in `~/config/settings/sen/bert.settings` (`#` starts a comment), changed in the settings window of SEN. The options of the command
 * line (`--auto`, `--wipe`) win over them for one run. Plain C++ (tests/BertSettingsTest.cpp runs everywhere).
 */
struct Settings {
	/** if OpenLibrary finds several books: ask which one is yours; off: the best one is taken, as `--auto` does */
	bool choose = true;
	/** replace the attributes that the file already has (and those of the people); off: only the missing ones are filled */
	bool overwrite = true;
	/** make a person of each author of the book (a file next to it, with the Authorship relation) */
	bool authors = true;
	/** the most authors that are made, for books with a long list */
	int maxAuthors = 100;
};

/** Reads `key = value` lines into the settings; unknown keys and bad values are ignored (what is there stays). */
void ParseSettings(const std::string& text, Settings* settings);

/**
 * Whether a value can be the value of a setting, by the rules that ParseSettings() applies silently, so that the settings window can say what is wrong (`reason` gets it).
 * A value with a `#` or a line break cannot be stored in the file.
 */
bool ValidateSetting(const std::string& key, const std::string& value, std::string* reason = nullptr);

}	// namespace bert
