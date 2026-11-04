/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "BertSettings.h"

#include <cstdlib>

#include "../../common/SettingsText.h"

namespace bert {

namespace st = sensei::settingstext;

void
ParseSettings(const std::string& text, Settings* settings)
{
	for (const auto& pair : st::Pairs(text)) {
		const std::string& key = pair.first;
		const std::string& value = pair.second;
		if (key == "authors")
			settings->authors = st::ParseBool(value, settings->authors);
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
	if (key == "authors")
		return st::IsBool(v) ? true : fail("yes or no");
	return fail("no such setting");
}

}	// namespace bert
