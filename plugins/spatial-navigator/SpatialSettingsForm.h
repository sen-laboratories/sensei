/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "../../common/SettingsSession.h"
#include "SpatialSettings.h"

namespace spatial {

/** Reads the settings file: the settings as they are stored. */
void LoadSettings(Settings* settings);

/**
 * The settings of the spatial navigator for the settings window of SEN (see common/SettingsSession.h): the mirror of the Overpass API in a menu (the servers of the list and `Custom
 * mirror`), and for the custom one an address and a key, which are enabled only while it is chosen (`enabled_when` of the schema).
 */
class SettingsProvider : public sensei::SettingsProvider {
public:
	virtual void		Describe(BMessage* settings, BMessage* schema, BMessage* defaults);
	virtual status_t	Apply(const BMessage& changes, std::vector<std::string>* refused);

private:
	std::string			fTestResult;		///< what the last press of "Test the server" found (shown next to the button, never stored)
};

/**
 * Asks the Overpass server of the settings (the mirror, with its key, as the navigator does) a small question about the centre of Wien, and says in a sentence what came of it: that it works
 * and how long it took, or why not (busy, refused, no such address, no answer, not an Overpass server).
 */
std::string TestOverpassServer(const Settings& settings);

}	// namespace spatial
