/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "../../common/SettingsSession.h"
#include "core/CalendarSettings.h"

namespace calendar {

/** Reads the settings file: the settings as they are stored. */
void LoadSettings(Settings* settings);

/**
 * The settings of the calendar extractor for the settings window of SEN (see common/SettingsSession.h): whether the file is named after the event, whether the description is kept in a
 * Markdown file, and who the user is (a field with a list of all the people that SEN knows, which follows what is typed).
 */
class SettingsProvider : public sensei::SettingsProvider {
public:
	virtual void		Describe(BMessage* settings, BMessage* schema, BMessage* defaults);
	virtual status_t	Apply(const BMessage& changes, std::vector<std::string>* refused);
};

}	// namespace calendar
