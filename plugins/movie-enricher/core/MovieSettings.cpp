/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "MovieSettings.h"

#include <cctype>
#include <cstdlib>

#include "../../../common/SettingsText.h"

namespace movie {

namespace st = sensei::settingstext;

namespace {

bool
IsNumber(const std::string& v, int low, int high)
{
	char* end = nullptr;
	const long n = std::strtol(v.c_str(), &end, 10);
	return !v.empty() && *end == '\0' && n >= low && n <= high;
}

/** `en` or `en-US` */
bool
IsLanguage(const std::string& v)
{
	if (v.size() != 2 && v.size() != 5)
		return false;
	for (size_t i = 0; i < v.size(); i++) {
		const unsigned char c = (unsigned char)v[i];
		if (i < 2 ? !std::islower(c) : (i == 2 ? c != '-' : !std::isupper(c)))
			return false;
	}
	return true;
}

/** a key is a word: letters, digits, `-` and `_` (v3 keys are 32 hexadecimal digits), and `.` between the three parts of the read access token (v4) */
bool
IsKey(const std::string& v)
{
	for (unsigned char c : v) {
		if (!std::isalnum(c) && c != '-' && c != '_' && c != '.')
			return false;
	}
	return true;
}

}	// namespace


void
ParseSettings(const std::string& text, Settings* settings)
{
	for (const auto& pair : st::Pairs(text)) {
		const std::string& key = pair.first;
		const std::string& value = pair.second;
		if (key == "api_key") {
			if (IsKey(value))
				settings->apiKey = value;
		} else if (key == "language") {
			if (IsLanguage(value))
				settings->language = value;
		} else if (key == "crew") {
			settings->crew = st::ParseBool(value, settings->crew);
		} else if (key == "cast") {
			settings->cast = st::ParseBool(value, settings->cast);
		}
	}
}


bool
ValidateSetting(const std::string& key, const std::string& value, std::string* reason)
{
	auto fail = [reason](const char* text) {
		if (reason != nullptr)
			*reason = text;
		return false;
	};

	const std::string v = st::Trim(value);
	if (v.find_first_of("#\n\r") != std::string::npos)
		return fail("a # or a line break cannot be stored in the settings file");
	if (key == "api_key")
		return IsKey(v) ? true : fail("an API key (v3) or the API Read Access Token (v4) of TMDB: letters and digits only, no blanks");
	if (key == "language")
		return IsLanguage(v) ? true : fail("a language like en-US or de-DE");
	if (key == "crew" || key == "cast")
		return st::IsBool(v) ? true : fail("yes or no");
	return fail("no such setting");
}

}	// namespace movie
