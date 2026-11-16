/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

#include <Entry.h>
#include <Message.h>

/**
 * What the plugin does: finds out that a calendar file is an event (identify), writes what the file says into its attributes, names the file after the event and keeps its description
 * in a Markdown file beside it (enrich), and works out the relations of an event when they are opened (resolve): its attendees, its place and the instances of its series.
 */
class CalendarService {
public:
	struct Options {
		bool	rename = true;			///< name the file after the event (2026-11-12_0930_Title.ics)
		bool	description = true;		///< keep the description in a Markdown file beside it
		std::string	myself;				///< who the user is (`Name <address>`): the attendee that is the user is told in the relation Attendee
	};

							CalendarService() {}
	explicit				CalendarService(const Options& options) : fOptions(options) {}

	/** Whether the file is a calendar with one event: sets its semantic type (the reply has it). B_ENTRY_NOT_FOUND if it is not. */
	status_t				Identify(const entry_ref& file, BMessage* reply);

	/** Writes the attributes; @p file is the new ref if the file was renamed. */
	status_t				Enrich(entry_ref* file, BMessage* reply);

	/**
	 * The targets of a relation of the event of the file (`relation/x-vnd.sen-labs.relation.event.attendee`, `.location`, `.series`), added to @p reply as sensei::key::kTargets.
	 * @param libraries the Library folders that the server gave (sensei::key::kLibraries), which the entities are made in
	 */
	status_t				Resolve(const entry_ref& file, const char* relationType, const BMessage& libraries, BMessage* reply);

	/** Prints what would be written for the event of the file, and the relations that it would give (for the command line). */
	status_t				Describe(const entry_ref& file);

	/** A short title and text for the notification that tells what was done. */
	const std::string&		SummaryTitle() const { return fSummaryTitle; }
	const std::string&		Summary() const { return fSummary; }

private:
	Options					fOptions;
	std::string				fSummaryTitle, fSummary;
};
