/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "../../common/SettingsSession.h"
#include "core/PersonSettings.h"

namespace robin {

/** Reads the settings file (`~/config/settings/sen/robin.settings`): the settings as they are stored. */
void LoadSettings(Settings* settings);

/** The settings of Robin for the settings window of SEN (see common/SettingsSession.h): the fields of the message are the keys of the file. */
class SettingsProvider : public sensei::SettingsProvider {
public:
	virtual void		Describe(BMessage* settings, BMessage* schema, BMessage* defaults);
	virtual status_t	Apply(const BMessage& changes, std::vector<std::string>* refused);
};

}	// namespace robin
