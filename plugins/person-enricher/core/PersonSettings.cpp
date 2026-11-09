/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PersonSettings.h"

#include <cstdlib>

#include "../../../common/SettingsText.h"

namespace robin {

namespace st = sensei::settingstext;

namespace {

bool
IsBoolKey(const std::string& key)
{
	return key == "email_services" || key == "github" || key == "fediverse" || key == "bluesky" || key == "feeds" || key == "photo";
}


// a whole number in the range; false for anything else (a number with a fraction, text, nothing)
bool
ParseSize(const std::string& value, int* number)
{
	const std::string v = st::Trim(value);
	if (v.empty() || v.size() > 6)
		return false;
	for (char c : v) {
		if (c < '0' || c > '9')
			return false;
	}
	*number = atoi(v.c_str());
	return *number >= kMinPhotoSize && *number <= kMaxPhotoSize;
}

}	// namespace


void
ParseSettings(const std::string& text, Settings* settings)
{
	for (const auto& pair : st::Pairs(text)) {
		const std::string& key = pair.first;
		const std::string& value = pair.second;
		if (key == "email_services")
			settings->emailServices = st::ParseBool(value, settings->emailServices);
		else if (key == "github")
			settings->github = st::ParseBool(value, settings->github);
		else if (key == "fediverse")
			settings->fediverse = st::ParseBool(value, settings->fediverse);
		else if (key == "bluesky")
			settings->bluesky = st::ParseBool(value, settings->bluesky);
		else if (key == "feeds")
			settings->feeds = st::ParseBool(value, settings->feeds);
		else if (key == "photo")
			settings->photo = st::ParseBool(value, settings->photo);
		else if (key == "photo_size") {
			int size = 0;
			if (ParseSize(value, &size))
				settings->photoSize = size;
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
	if (IsBoolKey(key))
		return st::IsBool(v) ? true : fail("yes or no");
	if (key == "photo_size") {
		int size = 0;
		return ParseSize(v, &size) ? true : fail("a whole number of pixels from 64 to 512");
	}
	return fail("no such setting");
}

}	// namespace robin
