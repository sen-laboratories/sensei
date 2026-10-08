/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Application.h>
#include <Entry.h>

/**
 * @brief Extracts the attributes of a MIME type: the attribute info of its entry in the MIME database, as the contents of the type.
 *
 * A type contains its attributes (the relation "contains"), which are of the type meta/x-vnd.sen-labs.meta-mime-attribute and
 * have the properties name, type, displayable, editable, searchable and width.
 */
class App : public BApplication
{
public:
					App();

	virtual void	RefsReceived(BMessage* message);
	virtual void	ArgvReceived(int32 argc, char** argv);

private:
	/** The attributes of the type that the file stands for, in the format of the plugin result (an item with one entry in every field per attribute). */
	status_t		ExtractAttributes(const entry_ref* ref, BMessage* reply);
};
