/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

#include "IcsText.h"

/**
 * @file CalendarSettings.h
 * @brief The settings of the calendar extractor: lines of `key = value` in `~/config/settings/sen/calendar-extractor.settings` (`#` starts a comment), changed in the settings window of SEN. Plain C++.
 */

namespace calendar {

struct Settings {
	bool		rename = true;			///< name the file after the event (2026-11-12_0930_Title.ics)
	/** the name after the pattern: tokens {date} {time} {year} {month} {day} {hour} {minute} {title} (see ics::BuildEventName) */
	std::string	renamePattern = ics::kDefaultNamePattern;
	bool		description = true;		///< keep the description in a Markdown file beside the event
	/** who the user is, as `Name <address>` (or only the address, or only the name): the attendee that is the user is told in the relation Attendee */
	std::string	myself;
};

/** Reads `key = value` lines into the settings; unknown keys and bad values are ignored (what is there stays). */
void ParseSettings(const std::string& text, Settings* settings);

/** Whether a value can be the value of a setting, so that the settings window can say what is wrong; `reason` gets it. */
bool ValidateSetting(const std::string& key, const std::string& value, std::string* reason = nullptr);

/** `Jane Roe <jane@example.org>` as the name and the address that it names (either may be empty). */
void SplitPerson(const std::string& text, std::string* name, std::string* email);

/** Whether a person of an event is the user: the address is the same (any case), or, if the setting names no address, the name is (as people say it: `Roe, Jane` is `Jane Roe`). */
bool IsMyself(const Settings& settings, const std::string& name, const std::string& email);

}	// namespace calendar
