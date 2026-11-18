/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

#include <Entry.h>
#include <Node.h>

#include "core/CalendarFacts.h"

/**
 * The files of the plugin: reading the calendar file, writing what is known about the event into its attributes, naming the file after the event and keeping its description in a
 * Markdown file beside it. Everything about BFS is here, so that the service says what it found and not how it is stored.
 */
namespace calendar {
namespace store {

/** The text of a file, at most @p limit bytes; false if it is too large or cannot be read. */
bool ReadText(const entry_ref& file, size_t limit, std::string* text);

/** Sets the type of the file (text/calendar if it has none) and, if @p semanticType is not NULL, its semantic type META:TYPE. */
status_t SetTypes(const entry_ref& file, const char* semanticType);

/**
 * Writes what the calendar file says about the event into the attributes. What comes from the file is replaced on every run (a place that the file no longer names is taken away);
 * what only a person sets (the type of the event) is not touched.
 */
status_t WriteEvent(BNode& node, const Facts& facts);

/** The attributes of a calendar with several events: its name, its description, the number of events and the time that they cover. */
status_t WriteCalendar(BNode& node, const std::string& name, const std::string& description, int32 events, int64_t first, int64_t last, bool hasTimes);

/**
 * Renames the file to @p baseName with its own extension (`.ics`, else `.ics` is added); a name that is taken by another file gets `-2`, `-3`... Gets the new ref.
 * @return B_OK if the file has that name now (also if it had it already)
 */
status_t Rename(entry_ref* file, const std::string& baseName);

/**
 * The description of the event as a Markdown file with the name of the event beside it (`2026-11-12_0930_Planning.md`). A file that is there and that this plugin made is made again; one that
 * somebody else made (or edited) is left alone. Nothing is written for an event with no description (and a file that was made earlier for one is not removed).
 * @param created gets the file if it was written
 */
status_t WriteDescriptionFile(const entry_ref& beside, const std::string& baseName, const std::string& title, const std::string& markdown, entry_ref* created);

/**
 * The file of an event that is kept (the entity), found by its identifier: asked of the file systems (the attribute is indexed), else looked for in @p folder. A file in the trash is not one.
 * @param onlyInFolder look in @p folder only (a copy of the event is wanted there, whatever is kept elsewhere)
 * @return false if there is none
 */
bool FindEventFile(const std::string& key, const entry_ref* folder, entry_ref* found, bool onlyInFolder = false);

/** A new file in @p folder, named after @p title (`Title.ics`, `Title-2.ics`...), with the text: a calendar file that has the type text/calendar. Gets the ref. */
status_t CreateCalendarFile(const entry_ref& folder, const std::string& title, const std::string& text, entry_ref* created);

/** Replaces the text of a file. */
status_t ReplaceText(const entry_ref& file, const std::string& text);

}	// namespace store
}	// namespace calendar
