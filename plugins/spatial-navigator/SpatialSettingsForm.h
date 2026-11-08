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
};

}	// namespace spatial
