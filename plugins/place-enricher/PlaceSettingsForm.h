/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "../../common/SettingsSession.h"
#include "PlaceSettings.h"

namespace place {

/** Reads the settings file: the settings as they are stored (the options of the command line are not in it). */
void LoadSettings(Settings* settings);

/** The settings of the place enricher for the settings window of SEN (see common/SettingsSession.h): the fields of the message are the keys of the file. */
class SettingsProvider : public sensei::SettingsProvider {
public:
	virtual void		Describe(BMessage* settings, BMessage* schema, BMessage* defaults);
	virtual status_t	Apply(const BMessage& changes, std::vector<std::string>* refused);
};

}	// namespace place
