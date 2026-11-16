/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "CalendarSettings.h"

#include <ctype.h>

#include "../../../common/SettingsText.h"
#include "CalendarFacts.h"

namespace calendar {

namespace st = sensei::settingstext;

namespace {

std::string
Lower(const std::string& text)
{
	std::string out = text;
	for (char& c : out)
		c = (char)tolower((unsigned char)c);
	return out;
}

}	// namespace


void
ParseSettings(const std::string& text, Settings* settings)
{
	for (const auto& pair : st::Pairs(text)) {
		const std::string& key = pair.first;
		const std::string value = st::Trim(pair.second);
		if (key == "rename")
			settings->rename = st::ParseBool(value, settings->rename);
		else if (key == "description")
			settings->description = st::ParseBool(value, settings->description);
		else if (key == "myself")
			settings->myself = value;
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
	if (key == "rename" || key == "description")
		return st::IsBool(v) ? true : fail("yes or no");
	if (key == "myself")
		return true;
	return fail("no such setting");
}


void
SplitPerson(const std::string& text, std::string* name, std::string* email)
{
	std::string value = st::Trim(text);
	name->clear();
	email->clear();
	const size_t open = value.find('<');
	const size_t close = value.find('>');
	if (open != std::string::npos && close != std::string::npos && close > open) {
		*name = st::Trim(value.substr(0, open));
		*email = st::Trim(value.substr(open + 1, close - open - 1));
	} else if (value.find('@') != std::string::npos && value.find(' ') == std::string::npos) {
		*email = value;
	} else {
		*name = value;
	}
}


bool
IsMyself(const Settings& settings, const std::string& name, const std::string& email)
{
	if (settings.myself.empty())
		return false;
	std::string myName, myEmail;
	SplitPerson(settings.myself, &myName, &myEmail);
	if (!myEmail.empty())
		return !email.empty() && Lower(myEmail) == Lower(email);
	return !myName.empty() && !name.empty() && Lower(NaturalName(myName)) == Lower(NaturalName(name));
}

}	// namespace calendar
