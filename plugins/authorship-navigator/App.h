/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Application.h>

/**
 * @brief Navigator for the Authorship relation: opens the author with the application of the person.
 *
 * Navigators are named for the relation type that they handle. The file of an Authorship in a relation view is a proxy for the author (it carries
 * SEN:REL:TRG), and the relation type shows the icon of a person. Double-clicking it opens the author the way any person is opened: in the
 * application that is set for the type of the file (Shoji, with its template for people; the People application for a legacy contact). The
 * properties of the relation are not arguments that a person needs, so nothing is passed on; a relation whose properties matter for its target has a
 * navigator that maps them (the places in a document for DocumentReference, the types of an ontology).
 */
class App : public BApplication
{
public:
					App();

	virtual void	AboutRequested();
	virtual void	RefsReceived(BMessage* message);
	virtual void	ArgvReceived(int32 argc, char** argv);
};
