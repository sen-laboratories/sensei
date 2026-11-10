/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PlaceSettings.h"

#include <cctype>

#include "../../../common/SettingsText.h"

namespace place {

namespace st = sensei::settingstext;

namespace {

/** empty (the system's), `de` or `de-AT` */
bool
IsLanguage(const std::string& v)
{
	if (v.empty())
		return true;
	if (v.size() != 2 && v.size() != 5)
		return false;
	for (size_t i = 0; i < v.size(); i++) {
		const unsigned char c = (unsigned char)v[i];
		if (i < 2 ? !std::islower(c) : (i == 2 ? c != '-' : !std::isupper(c)))
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
		if (key == "language") {
			if (IsLanguage(value))
				settings->language = value;
		} else if (key == "lookup_by_position") {
			settings->lookupByPosition = st::ParseBool(value, settings->lookupByPosition);
		} else if (key == "near_me") {
			settings->nearMe = st::ParseBool(value, settings->nearMe);
		} else if (key == "picture") {
			settings->picture = st::ParseBool(value, settings->picture);
		} else if (key == "boundary") {
			settings->boundary = st::ParseBool(value, settings->boundary);
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
	if (key == "language")
		return IsLanguage(v) ? true : fail("a language like de or de-AT, or nothing for the language of the system");
	if (key == "lookup_by_position" || key == "near_me" || key == "picture" || key == "boundary")
		return st::IsBool(v) ? true : fail("yes or no");
	return fail("no such setting");
}

}	// namespace place
