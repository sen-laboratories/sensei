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
 * The settings of the spatial navigator for the settings window of SEN (see common/SettingsSession.h): the list of the Overpass servers (a switch, a name and a
 * description for each, in the order that they are asked; the rows can be moved), the key of the row that is selected, and the address of the custom mirror.
 */
class SettingsProvider : public sensei::SettingsProvider {
public:
	virtual void		Describe(BMessage* settings, BMessage* schema, BMessage* defaults);
	virtual status_t	Apply(const BMessage& changes, std::vector<std::string>* refused);

private:
	ServerTest			fTest;				///< what the last press of "Test the server" found (shown next to the button, never stored)
};

/**
 * Asks the first Overpass server of the list that is on and complete (with its key, as the navigator does) a small question about the centre of Wien, and says what came of it (see JudgeServerTest): the HTTP status as the short text, the icon of its state, the detail for the tool tip.
 */
ServerTest TestOverpassServer(const Settings& settings);

}	// namespace spatial
