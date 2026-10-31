/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include "../../common/TargetOpener.h"

/**
 * @brief Navigator for the relationships between people (Knows and its special cases: friend, colleague, mentor...) and from a person to an organization
 * (Employment, Membership): opens the person or organization that the relation ends at with its application (Shoji).
 *
 * Named for the relation type that the others are special cases of. The file of a relation in a relation view is a proxy for its target (see
 * TargetOpener.h); the relation types show the icon of a person (of an organization for Employment and Membership).
 */
class App : public sensei::TargetOpenerApp
{
public:
	App()
		:
		TargetOpenerApp("application/x-vnd.sen-plugin.sen-labs.knows-navigator", "SEN Knows Navigator")
	{
	}
};
