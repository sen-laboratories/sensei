/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

/**
 * @file IcsTime.h
 * @brief Dates and times of iCalendar (RFC 5545) and vCalendar 1.0, and their recurrence rules. Plain C++, no system calls.
 *
 * A time in a calendar file is a civil date and time that is UTC (`...Z`), in a named zone (`TZID=Europe/Vienna`) or floating (the local time of whoever reads it), or a whole day
 * (`VALUE=DATE`). It is kept as it was written (DateTime); turning it into the seconds since 1970 needs the zone (see IcsCalendar.h).
 */

namespace ics {

/** A civil date and time as written in the file. */
struct DateTime {
	int			year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	bool		valid = false;
	bool		dateOnly = false;	///< a day (VALUE=DATE): hour, minute and second are 0
	bool		utc = false;		///< written with a Z
	std::string	tzid;				///< the zone, if one was named; empty when UTC or floating
};

/** Days since 1970-01-01 of a civil date (proleptic Gregorian); the algorithm of Howard Hinnant. */
int64_t DaysFromCivil(int year, int month, int day);
void CivilFromDays(int64_t days, int* year, int* month, int* day);
/** 0 is Sunday. */
int DayOfWeek(int year, int month, int day);
int DaysInMonth(int year, int month);
bool IsLeapYear(int year);

/** The seconds that the civil time would be since 1970 if it were UTC. */
int64_t LocalSeconds(const DateTime& time);
/** The civil time of such a count of seconds (zone and flags are not set). */
DateTime FromLocalSeconds(int64_t seconds);

/**
 * Reads a date or a date and time, forgiving: `20261116`, `20261116T093000Z`, `20261116T0930`, `2026-11-16T09:30:00`, with blanks. @p tzid is the zone of the property (it is ignored for a UTC time).
 * @p dateOnly says that the property was VALUE=DATE, a value of 8 digits is a date as well.
 */
bool ParseDateTime(const std::string& value, const std::string& tzid, bool dateOnly, DateTime* out);

/** `P1DT2H30M`, `-PT15M`, `PT0S`, `P2W` (also the vCalendar 1.0 form `PT1H`); the seconds, false if it is none. */
bool ParseDuration(const std::string& text, int64_t* seconds);

/** `2026-11-16` or `2026-11-16T09:30:00` (the zone is not shown): for messages and tests. */
std::string DateTimeText(const DateTime& time);

/** A day of the week with the place among those of the month or year (`2MO` is the second Monday, `-1FR` the last Friday); 0 is every one. */
struct WeekDay {
	int ordinal = 0;
	int weekday = 0;	///< 0 Sunday ... 6 Saturday
};

enum class Frequency { None, Secondly, Minutely, Hourly, Daily, Weekly, Monthly, Yearly };

/** A recurrence rule (RRULE), also that of vCalendar 1.0 (`D1 #5`, `W1 MO TU #10`, `MP1 1+ MO #0`, `YM1 6 #3`). */
struct Rule {
	Frequency				frequency = Frequency::None;
	int						interval = 1;
	int						count = 0;			///< 0: no count
	DateTime				until;				///< valid: the last day or moment
	std::vector<int>		bySecond, byMinute, byHour, byMonthDay, byYearDay, byWeekNo, byMonth, bySetPos;
	std::vector<WeekDay>	byDay;
	int						weekStart = 1;		///< WKST, Monday
	bool					valid = false;
};

/** Reads the value of an RRULE (RFC 5545 `FREQ=WEEKLY;BYDAY=MO,WE`, or the form of vCalendar 1.0); forgiving about case and blanks. */
bool ParseRule(const std::string& text, Rule* rule);

/**
 * The instances of the rule from @p start in order, as civil times (their zone is that of @p start), until @p visit says to stop, the rule ends (count, until) or @p limit instances were visited.
 * The first instance is the start itself. An UNTIL in UTC should be given as local time by the caller: it is compared as the civil time that it holds.
 */
void Expand(const Rule& rule, const DateTime& start, const std::function<bool(const DateTime&)>& visit, int limit = 100000);

}	// namespace ics
