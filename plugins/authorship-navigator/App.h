/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include "../../common/TargetOpener.h"

/**
 * @brief Navigator for the Authorship relation: opens the author with the application of the person (Shoji, with its template for people).
 *
 * The file of an Authorship in a relation view is a proxy for the author (see TargetOpener.h); the relation type shows the icon of a person.
 */
class App : public sensei::TargetOpenerApp
{
public:
	App()
		:
		TargetOpenerApp("application/x-vnd.sen-plugin.sen-labs.authorship-navigator", "SEN Authorship Navigator")
	{
	}
};
