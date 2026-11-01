/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Application.h>
#include <Entry.h>

/**
 * @brief Navigator for the files of relations: opens the target of a relation with the application of its type (the fallback for a relation without a navigator of its own).
 *
 * The file of a relation in a relation view is a proxy for the target of the relation (it carries SEN:REL:TRG). Opening it opens the
 * folder that the target is in, with the target selected; for a target that is a folder, the folder itself. This is the default for all
 * relations (the supertype "relation"): relations that need more declare a navigator of their own for their type (e.g. the places in
 * a document, the types of an ontology).
 */
class App : public BApplication
{
public:
	virtual void	AboutRequested();
					App();

	virtual void	RefsReceived(BMessage* message);
	virtual void	ArgvReceived(int32 argc, char** argv);

private:
	/** The target of a relation file (itself for any other file). */
	static entry_ref	TargetOf(const entry_ref& ref, bool* found);
	/** Open the target the way a double click on it would. */
	status_t			Open(const entry_ref& target);
};
