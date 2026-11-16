/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <stdint.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "IcsParser.h"
#include "IcsTime.h"

/**
 * @file IcsCalendar.h
 * @brief The events of a calendar file: what they say (title, times, place, people, recurrence), with times as the seconds since 1970 (UTC). Plain C++.
 */

namespace ics {

struct Person {
	std::string	name;		///< CN, or the name before an address (`John Doe <john@example.org>`)
	std::string	email;		///< without `mailto:`
	std::string	role;		///< ROLE: REQ-PARTICIPANT, OPT-PARTICIPANT, CHAIR, NON-PARTICIPANT; the organizer has `ORGANIZER`
	std::string	status;		///< PARTSTAT: ACCEPTED, DECLINED, TENTATIVE, NEEDS-ACTION...
	std::string	type;		///< CUTYPE: INDIVIDUAL, GROUP, RESOURCE, ROOM
};

struct Event {
	std::string					uid, summary, description, location, url;
	std::string					status;			///< CONFIRMED, TENTATIVE, CANCELLED
	std::string					transparency;	///< OPAQUE or TRANSPARENT
	std::string					classification;	///< PUBLIC, PRIVATE, CONFIDENTIAL
	std::vector<std::string>	categories;
	bool						hasGeo = false;
	double						latitude = 0, longitude = 0;
	bool						hasOrganizer = false;
	Person						organizer;
	std::vector<Person>			attendees;

	DateTime					start, end;
	bool						hasStart = false, hasEnd = false;
	bool						allDay = false;			///< the start is a day (VALUE=DATE); the end is the day after the last one, as the file has it
	bool						hasDuration = false;
	int64_t						durationSeconds = 0;
	std::string					durationText;			///< ISO 8601 (PT1H30M): the one of the file, or worked out from start and end

	int64_t						startUnix = 0, endUnix = 0;		///< seconds since 1970 (UTC); valid with startKnown and endKnown
	bool						startKnown = false, endKnown = false;

	std::string					ruleText;
	Rule						rule;
	bool						hasRule = false;
	std::vector<DateTime>		exceptions, extraDates;		///< EXDATE, RDATE
	DateTime					recurrenceId;
	bool						hasRecurrenceId = false;	///< this event is one instance of a series (it may be moved)
	int							sequence = 0;
	int							index = 0;					///< the place of the VEVENT in the file
};

/** The zones that a file defines itself (VTIMEZONE), and the system for those that it does not. */
class TimeZones {
public:
	typedef std::function<bool(const std::string& tzid, int64_t localSeconds, int* offsetSeconds)> Fallback;

	void	Add(const Component& timezone);
	/** The offset from UTC (seconds, east is positive) in the zone at a local time; false if the zone is not known (neither defined in the file nor by the fallback). */
	bool	OffsetAt(const std::string& tzid, int64_t localSeconds, int* offsetSeconds) const;
	bool	Knows(const std::string& tzid) const;

	Fallback	fallback;		///< for a zone that the file does not define

private:
	struct Observance {
		DateTime			onset;			///< the first change, in local time (before it)
		int					offsetFrom = 0, offsetTo = 0;
		Rule				rule;
		bool				hasRule = false;
		std::vector<int64_t>	extra;		///< RDATE, as local seconds
	};
	std::map<std::string, std::vector<Observance> >	fZones;
};

/** The offset of a zone by the C library (TZ = the name): for `Europe/Vienna`; names that are not of the form Area/City are not taken (the library would silently use UTC for them). */
bool PosixZoneOffset(const std::string& tzid, int64_t localSeconds, int* offsetSeconds);
/** The offset of the local zone of the system at a local time (what a floating time and a day mean). */
bool LocalZoneOffset(int64_t localSeconds, int* offsetSeconds);

struct Calendar {
	std::string					name;			///< X-WR-CALNAME or NAME
	std::string					method;			///< PUBLISH, REQUEST, REPLY, CANCEL...
	std::string					version;		///< 2.0 or 1.0
	std::string					productId;
	std::string					timezone;		///< X-WR-TIMEZONE: the zone that the program of the owner shows it in
	std::vector<Event>			events;
	TimeZones					zones;
	std::vector<std::string>	notes;			///< what could not be read

	bool IsOldVersion() const { return version.compare(0, 2, "1.") == 0; }

	/** A time as the seconds since 1970 (UTC): a UTC time as it is, one with a zone by the zone, a floating one or a day by the local zone. False if the time is not valid. */
	bool	ToUnix(const DateTime& time, int64_t* seconds) const;
	/** The distinct events of the file: the VEVENTs of one UID (a series with moved instances) are one; the main one first. */
	std::vector<const Event*>	DistinctEvents() const;
	const Event*				MasterOf(const Event& event) const;
};

/** Reads a file; the zones of the system are the fallback. */
Calendar ReadCalendar(const std::string& text);

/** One instance of a series, worked out from the rule. */
struct Occurrence {
	DateTime	start;						///< the slot of the series (its RECURRENCE-ID)
	int64_t		startUnix = 0, endUnix = 0;
	std::string	summary;
	bool		moved = false;				///< the file has an instance of its own for this one, with other times
};

/** The instances of the series of an event just before and after it. False if the event is no part of a series (no rule, no master) or the rule cannot be used. */
bool SeriesNeighbours(const Calendar& calendar, const Event& event, bool* hasPrevious, Occurrence* previous, bool* hasNext, Occurrence* next);

/** `20261123T090000`, the RECURRENCE-ID of an instance (the selector of the place in the file). */
std::string OccurrenceKey(const DateTime& slot);

}	// namespace ics
