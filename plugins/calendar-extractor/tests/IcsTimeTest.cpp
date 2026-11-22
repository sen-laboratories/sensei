/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
// Plain C++ test of the dates, durations and recurrence rules.
#include "../core/IcsTime.h"

#include <stdio.h>

using namespace ics;

static int sChecks = 0, sFailures = 0;
#define CHECK(x) do { sChecks++; if (!(x)) { sFailures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static std::vector<std::string>
Run(const char* rule, const char* start, int limit = 12)
{
	Rule r;
	DateTime s;
	std::vector<std::string> out;
	if (!ParseRule(rule, &r) || !ParseDateTime(start, "", false, &s))
		return out;
	Expand(r, s, [&](const DateTime& t) { out.push_back(DateTimeText(t)); return true; }, limit);
	return out;
}

int main()
{
	// civil math
	CHECK(DaysFromCivil(1970, 1, 1) == 0 && DaysFromCivil(2000, 3, 1) == 11017);
	int y, m, d;
	CivilFromDays(DaysFromCivil(2026, 11, 16), &y, &m, &d);
	CHECK(y == 2026 && m == 11 && d == 16 && DayOfWeek(2026, 11, 16) == 1 && DayOfWeek(1970, 1, 1) == 4);
	CHECK(DaysInMonth(2024, 2) == 29 && DaysInMonth(2026, 2) == 28 && DaysInMonth(1900, 2) == 28 && DaysInMonth(2000, 2) == 29);

	// dates and times, also the forgiving ones
	DateTime t;
	CHECK(ParseDateTime("20261116T093000Z", "", false, &t) && t.utc && t.hour == 9 && t.minute == 30 && !t.dateOnly && t.tzid.empty());
	CHECK(ParseDateTime("20261116T093000", "Europe/Vienna", false, &t) && !t.utc && t.tzid == "Europe/Vienna");
	CHECK(ParseDateTime("20261116", "", false, &t) && t.dateOnly && t.hour == 0);
	CHECK(ParseDateTime("2026-11-16T09:30:00Z", "", false, &t) && t.utc && t.minute == 30);
	CHECK(ParseDateTime(" 20261116T0930 ", "", false, &t) && t.hour == 9 && t.second == 0);
	CHECK(ParseDateTime("20261116T093000", "", true, &t) && t.dateOnly && t.hour == 0);
	CHECK(ParseDateTime("20261116T240000", "", false, &t) && t.day == 17 && t.hour == 0);
	CHECK(!ParseDateTime("2026-13-01", "", false, &t) && !ParseDateTime("hello", "", false, &t) && !ParseDateTime("", "", false, &t));

	// durations
	int64_t seconds = 0;
	CHECK(ParseDuration("PT1H30M", &seconds) && seconds == 5400);
	CHECK(ParseDuration("-PT15M", &seconds) && seconds == -900);
	CHECK(ParseDuration("P1DT2H", &seconds) && seconds == 93600);
	CHECK(ParseDuration("P2W", &seconds) && seconds == 14 * 86400);
	CHECK(ParseDuration("PT0S", &seconds) && seconds == 0);
	CHECK(!ParseDuration("1H", &seconds) && !ParseDuration("P", &seconds) && !ParseDuration("PT", &seconds));

	// RFC 5545 examples (section 3.8.5.3), the first instances
	auto same = [](const std::vector<std::string>& got, std::vector<std::string> want) {
		return got.size() >= want.size() && std::vector<std::string>(got.begin(), got.begin() + want.size()) == want;
	};
	CHECK(same(Run("FREQ=DAILY;COUNT=10", "19970902T090000"), {"1997-09-02T09:00:00", "1997-09-03T09:00:00", "1997-09-04T09:00:00"}) && Run("FREQ=DAILY;COUNT=10", "19970902T090000").size() == 10);
	CHECK(Run("FREQ=DAILY;UNTIL=19971224T000000Z", "19970902T090000", 1000).size() == 113);
	CHECK(same(Run("FREQ=DAILY;INTERVAL=2", "19970902T090000"), {"1997-09-02T09:00:00", "1997-09-04T09:00:00", "1997-09-06T09:00:00"}));
	CHECK(same(Run("FREQ=DAILY;INTERVAL=10;COUNT=5", "19970902T090000"), {"1997-09-02T09:00:00", "1997-09-12T09:00:00", "1997-09-22T09:00:00", "1997-10-02T09:00:00", "1997-10-12T09:00:00"}));
	CHECK(same(Run("FREQ=WEEKLY;COUNT=4", "19970902T090000"), {"1997-09-02T09:00:00", "1997-09-09T09:00:00", "1997-09-16T09:00:00", "1997-09-23T09:00:00"}));
	// every other week on Monday, Wednesday and Friday, week starting on Sunday
	CHECK(same(Run("FREQ=WEEKLY;INTERVAL=2;WKST=SU;BYDAY=TU,TH", "19970902T090000", 8),
		{"1997-09-02T09:00:00", "1997-09-04T09:00:00", "1997-09-16T09:00:00", "1997-09-18T09:00:00", "1997-09-30T09:00:00", "1997-10-02T09:00:00"}));
	CHECK(same(Run("FREQ=WEEKLY;UNTIL=19971007T000000Z;WKST=SU;BYDAY=TU,TH", "19970902T090000", 30),
		{"1997-09-02T09:00:00", "1997-09-04T09:00:00", "1997-09-09T09:00:00", "1997-09-11T09:00:00"}) && Run("FREQ=WEEKLY;UNTIL=19971007T000000Z;WKST=SU;BYDAY=TU,TH", "19970902T090000", 30).size() == 10);
	// monthly: the first Friday; the last Monday; the 15th and the last day
	CHECK(same(Run("FREQ=MONTHLY;COUNT=10;BYDAY=1FR", "19970905T090000"), {"1997-09-05T09:00:00", "1997-10-03T09:00:00", "1997-11-07T09:00:00", "1997-12-05T09:00:00"}));
	CHECK(same(Run("FREQ=MONTHLY;BYDAY=-1MO", "19970929T090000"), {"1997-09-29T09:00:00", "1997-10-27T09:00:00", "1997-11-24T09:00:00"}));
	CHECK(same(Run("FREQ=MONTHLY;BYMONTHDAY=-3", "19970928T090000"), {"1997-09-28T09:00:00", "1997-10-29T09:00:00", "1997-11-28T09:00:00"}));
	CHECK(same(Run("FREQ=MONTHLY;COUNT=10;BYMONTHDAY=2,15", "19970902T090000"), {"1997-09-02T09:00:00", "1997-09-15T09:00:00", "1997-10-02T09:00:00", "1997-10-15T09:00:00"}));
	CHECK(same(Run("FREQ=MONTHLY;INTERVAL=18;COUNT=10;BYMONTHDAY=10,11,12,13,14,15", "19970910T090000"),
		{"1997-09-10T09:00:00", "1997-09-11T09:00:00", "1997-09-12T09:00:00", "1997-09-13T09:00:00", "1997-09-14T09:00:00", "1997-09-15T09:00:00", "2999-01-01T00:00:00"}) == false);
	CHECK(Run("FREQ=MONTHLY;INTERVAL=18;COUNT=10;BYMONTHDAY=10,11,12,13,14,15", "19970910T090000")[6] == "1999-03-10T09:00:00");
	// the second to last weekday of the month: BYSETPOS
	CHECK(same(Run("FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-2", "19970929T090000"), {"1997-09-29T09:00:00", "1997-10-30T09:00:00", "1997-11-27T09:00:00"}));
	// yearly: June and July; the 20th Monday; a week number; Thursdays of March; every four years the US election day
	CHECK(same(Run("FREQ=YEARLY;COUNT=10;BYMONTH=6,7", "19970610T090000"), {"1997-06-10T09:00:00", "1997-07-10T09:00:00", "1998-06-10T09:00:00", "1998-07-10T09:00:00"}));
	CHECK(same(Run("FREQ=YEARLY;BYDAY=20MO", "19970519T090000"), {"1997-05-19T09:00:00", "1998-05-18T09:00:00", "1999-05-17T09:00:00"}));
	CHECK(same(Run("FREQ=YEARLY;BYWEEKNO=20;BYDAY=MO", "19970512T090000"), {"1997-05-12T09:00:00", "1998-05-11T09:00:00", "1999-05-17T09:00:00"}));
	CHECK(same(Run("FREQ=YEARLY;BYMONTH=3;BYDAY=TH", "19970313T090000"), {"1997-03-13T09:00:00", "1997-03-20T09:00:00", "1997-03-27T09:00:00", "1998-03-05T09:00:00"}));
	CHECK(same(Run("FREQ=YEARLY;INTERVAL=4;BYMONTH=11;BYDAY=TU;BYMONTHDAY=2,3,4,5,6,7,8", "19961105T090000"), {"1996-11-05T09:00:00", "2000-11-07T09:00:00", "2004-11-02T09:00:00"}));
	// Friday the 13th
	CHECK(same(Run("FREQ=MONTHLY;BYDAY=FR;BYMONTHDAY=13", "19980213T090000"), {"1998-02-13T09:00:00", "1998-03-13T09:00:00", "1998-11-13T09:00:00", "1999-08-13T09:00:00"}));
	// every day in January for 3 years; a rule that never matches ends
	CHECK(Run("FREQ=YEARLY;UNTIL=20000131T140000Z;BYMONTH=1;BYDAY=SU,MO,TU,WE,TH,FR,SA", "19980101T090000", 1000).size() == 93);
	CHECK(Run("FREQ=MONTHLY;BYMONTHDAY=31;BYMONTH=2", "19980131T090000", 5).size() <= 1);
	CHECK(same(Run("FREQ=YEARLY", "20240229T090000", 3), {"2024-02-29T09:00:00", "2028-02-29T09:00:00", "2032-02-29T09:00:00"}));
	// hourly and every 15 minutes
	CHECK(same(Run("FREQ=HOURLY;INTERVAL=3;COUNT=3", "19970902T090000"), {"1997-09-02T09:00:00", "1997-09-02T12:00:00", "1997-09-02T15:00:00"}));
	CHECK(same(Run("FREQ=MINUTELY;INTERVAL=90;COUNT=3", "19970902T090000"), {"1997-09-02T09:00:00", "1997-09-02T10:30:00", "1997-09-02T12:00:00"}));
	// a day: all-day events keep the day only
	CHECK(same(Run("FREQ=WEEKLY;BYDAY=MO", "20261116", 3), {"2026-11-16", "2026-11-23", "2026-11-30"}));

	// vCalendar 1.0 rules
	CHECK(same(Run("D1 #3", "19960418T120000"), {"1996-04-18T12:00:00", "1996-04-19T12:00:00", "1996-04-20T12:00:00"}) && Run("D1 #3", "19960418T120000").size() == 3);
	CHECK(same(Run("W2 TU TH #4", "19960416T120000"), {"1996-04-16T12:00:00", "1996-04-18T12:00:00", "1996-04-30T12:00:00", "1996-05-02T12:00:00"}) && Run("W2 TU TH #4", "19960416T120000").size() == 4);
	CHECK(same(Run("MP1 1+ MO #3", "19960401T090000"), {"1996-04-01T09:00:00", "1996-05-06T09:00:00", "1996-06-03T09:00:00"}));
	CHECK(same(Run("MD1 15 #3", "19960415T090000"), {"1996-04-15T09:00:00", "1996-05-15T09:00:00", "1996-06-15T09:00:00"}));
	CHECK(same(Run("MD1 1- #3", "19960430T090000"), {"1996-04-30T09:00:00", "1996-05-31T09:00:00", "1996-06-30T09:00:00"}));
	CHECK(same(Run("YM1 6 #3", "19960615T090000"), {"1996-06-15T09:00:00", "1997-06-15T09:00:00", "1998-06-15T09:00:00"}));
	CHECK(same(Run("D1 19960420T120000Z", "19960418T120000"), {"1996-04-18T12:00:00", "1996-04-19T12:00:00", "1996-04-20T12:00:00"}));

	// bad rules
	Rule r;
	CHECK(!ParseRule("", &r) && !ParseRule("nonsense", &r) && !ParseRule("FREQ=SOMETIMES", &r));
	CHECK(ParseRule(" freq=weekly ; byday=mo,we ; interval=2 ", &r) && r.frequency == Frequency::Weekly && r.interval == 2 && r.byDay.size() == 2);

	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
