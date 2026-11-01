/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include "../../common/TargetOpener.h"

/**
 * @brief Navigator for the PersonalRelation (what people are to one another: knows, friend, colleague, mentor...) and for the relations from a
 * person to an organization (Employment, Membership): opens the person or organization that the relation ends at with its application (Shoji).
 *
 * The file of a relation in a relation view is a proxy for its target (see TargetOpener.h); the relation types show the icon of a person (of an
 * organization for Employment and Membership).
 */
class App : public sensei::TargetOpenerApp
{
public:
	App()
		:
		TargetOpenerApp("application/x-vnd.sen-plugin.sen-labs.personal-relation-navigator", "SEN Personal Relation Navigator")
	{
	}
};
