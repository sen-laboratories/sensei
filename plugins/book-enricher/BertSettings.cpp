/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "BertSettings.h"

#include <cstdlib>

#include "../../common/SettingsText.h"

namespace bert {

namespace st = sensei::settingstext;

namespace {

bool
IsNumber(const std::string& v, int low, int high)
{
	char* end = nullptr;
	const long n = std::strtol(v.c_str(), &end, 10);
	return !v.empty() && *end == '\0' && n >= low && n <= high;
}

}	// namespace


void
ParseSettings(const std::string& text, Settings* settings)
{
	for (const auto& pair : st::Pairs(text)) {
		const std::string& key = pair.first;
		const std::string& value = pair.second;
		if (key == "choose")
			settings->choose = st::ParseBool(value, settings->choose);
		else if (key == "overwrite")
			settings->overwrite = st::ParseBool(value, settings->overwrite);
		else if (key == "authors")
			settings->authors = st::ParseBool(value, settings->authors);
		else if (key == "max_authors" && IsNumber(value, 1, 100))
			settings->maxAuthors = std::atoi(value.c_str());
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
	if (key == "choose" || key == "overwrite" || key == "authors")
		return st::IsBool(v) ? true : fail("yes or no");
	if (key == "max_authors")
		return IsNumber(v, 1, 100) ? true : fail("a number from 1 to 100");
	return fail("no such setting");
}

}	// namespace bert
