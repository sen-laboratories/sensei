/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Application.h>
#include <String.h>

/**
 * @brief Navigator for references: opens the target of a relation.
 *
 * It is the navigator of the generic reference relation, and of what a type contains. It is not bound to a kind of target: what the
 * target is decides how it is opened, and a new kind of target gets its branch in `App::RefsReceived`:
 *  - a type of the MIME database (what an ontology provides, or what contains attributes) is shown in FileTypes, selected,
 *  - an attribute of a type (the contents of a type) is opened in the attribute window of FileTypes, which is done like a user
 *    does it: the application is scripted with what every application has (its windows and views, by name and index),
 *  - any other file is opened as usual, with its preferred application.
 *
 * The file of a relation in a relation view is a proxy for its target: it carries the target (SEN:REL:TRG) and, for an attribute,
 * its name (SEN:attr:name); the same is in the properties of the message when an item of a menu is chosen.
 */
class App : public BApplication
{
public:
					App();

	virtual void	RefsReceived(BMessage* message);
	virtual void	ArgvReceived(int32 argc, char** argv);

private:
	/** Show the type in FileTypes, and the attribute of the type (or of its supertype) if one is named. */
	status_t		OpenMimeType(const BString& mimeType, const BString& attribute);
};
