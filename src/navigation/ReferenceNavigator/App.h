/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Application.h>

/**
 * @brief Navigator for generic references: opens the target of a reference.
 *
 * What a generic reference points to decides how it is opened. A type of the MIME database (the relations of an ontology to the
 * types that it provides) is shown in FileTypes, selected; any other file is opened as usual, with its preferred application.
 */
class App : public BApplication
{
public:
					App();

	virtual void	RefsReceived(BMessage* message);
	virtual void	ArgvReceived(int32 argc, char** argv);
};
