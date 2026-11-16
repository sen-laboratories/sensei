/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "IcsCalendar.h"

/**
 * @file CalendarFacts.h
 * @brief What is written about an event: its attributes as the ontology has them, worked out from the calendar file. Plain C++ (the plugin only stores it).
 */

namespace calendar {

struct Facts {
	std::string		title;
	std::string		summary;			///< the first lines of the description, plain, for a column (dc:description)
	std::string		markdown;			///< the whole description as Markdown (the file beside the event); empty if there is none
	std::string		uid, location, organizer, url;
	std::string		status;				///< Confirmed, Tentative, Cancelled, or empty
	std::string		attendanceMode;		///< Offline, Online, Mixed, or empty
	std::string		meetingUrl, meetingType;
	std::string		recurrence;
	std::vector<std::string>	links;	///< all the links in the event, in the order of the file (the description, the location, the conference properties)
	bool			hasStart = false, hasEnd = false;
	int64_t			startUnix = 0, endUnix = 0;		///< seconds since 1970 (UTC)
	std::string		startEdtf, endEdtf;		///< the civil text of a time that no zone says (a day, a floating time): the attribute that re-derives it when the zone changes
	std::string		duration;				///< only for an event that has a start and no end
	bool			allDay = false;
	ics::DateTime	localStart;				///< the start in the local zone (a day: the day), for the name of the file
	std::string		baseName;				///< `2026-11-12_0930_Title`
};

/** What the plugin writes for an event of a calendar. */
Facts FactsOf(const ics::Calendar& calendar, const ics::Event& event);

/** The attendees as `Name <address>` (or one of them), for the organizer attribute and for messages. */
std::string PersonText(const ics::Person& person);

/** The words of the ontology for what the file says: the event status (Confirmed, Tentative, Cancelled), a response (Accepted, Declined, Tentative, Needs action, Delegated), a role (organizer, required, optional, chair, resource); empty if there is none. */
std::string EventStatusText(const std::string& status);
std::string ResponseText(const std::string& partstat);
std::string RoleText(const ics::Person& person);

}	// namespace calendar
