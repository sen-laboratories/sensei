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
 * The settings of the spatial navigator for the settings window of SEN (see common/SettingsSession.h): the list of the Overpass servers (a switch, a name, a
 * description and the status of each, in the order that they are asked; the rows can be moved), the key of the row that is selected, and the address of the custom mirror. A server that is
 * switched on (or whose key or address changed) is tested in the background, and the status column shows what came of it.
 */
class SettingsProvider : public sensei::SettingsProvider {
public:
	virtual void		Describe(BMessage* settings, BMessage* schema, BMessage* defaults);
	virtual status_t	Apply(const BMessage& changes, std::vector<std::string>* refused);
};

/** The application gets this message when a test in the background is done: it sends the settings to the window again (SettingsSession::Push). */
const uint32 kPushSettings = 'SPsh';

/**
 * Asks an Overpass server (with its key, as the navigator does) a small question about the centre of Wien, and says what came of it (see JudgeServerTest): the HTTP status as the short text, the
 * state for the icon (`ok`, `warning`, `error`), the detail for the tool tip. It takes up to 20 seconds.
 */
ServerTest TestOverpassServer(const Server& server);

}	// namespace spatial
