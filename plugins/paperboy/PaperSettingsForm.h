/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "../../common/SettingsSession.h"

namespace paperboy {

/**
 * The settings of paperboy for the settings window of SEN (see common/SettingsSession.h). They are kept as before, in `~/config/settings/sen/paperboy.settings`, as lines of
 * `key = value`: the fields of the message are the keys of the file, and a change is put into the file as the line of its key (the comments and the other lines stay).
 * The command line and the environment still override the file for one run; the window shows and changes the file.
 */
class SettingsProvider : public sensei::SettingsProvider {
public:
	virtual void		Describe(BMessage* settings, BMessage* schema, BMessage* defaults);
	virtual status_t	Apply(const BMessage& changes, std::vector<std::string>* refused);
};

}	// namespace paperboy
