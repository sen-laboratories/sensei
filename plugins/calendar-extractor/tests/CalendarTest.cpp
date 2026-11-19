/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
// Plain C++ test of the calendar layer: events, zones, series. The files are those of tests/fixtures (the folder is FIXTURES).
#include "../core/IcsCalendar.h"

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <sstream>

using namespace ics;

static int sChecks = 0, sFailures = 0;
#define CHECK(x) do { sChecks++; if (!(x)) { sFailures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static std::string
Read(const char* name)
{
	std::ifstream in(std::string(FIXTURES) + "/" + name, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

/** UTC as seconds */
static int64_t
U(int y, int m, int d, int hh = 0, int mm = 0, int ss = 0)
{
	return DaysFromCivil(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss;
}

int main()
{
	// (what a floating time and a day mean is the zone of this computer: the tests say which)
	setenv("TZ", "Europe/Vienna", 1);
	tzset();

	// ---- Google: UTC, people, text
	{
		Calendar cal = ReadCalendar(Read("google.ics"));
		CHECK(cal.version == "2.0" && cal.method == "PUBLISH" && cal.name == "Team" && cal.timezone == "Europe/Vienna" && cal.events.size() == 1);
		const Event& e = cal.events[0];
		CHECK(e.uid == "abc123@google.com" && e.summary == "Planning" && e.status == "CONFIRMED" && e.sequence == 2 && e.transparency == "OPAQUE");
		CHECK(e.startKnown && e.startUnix == U(2026, 11, 16, 8, 30) && e.endKnown && e.endUnix == U(2026, 11, 16, 9, 30) && !e.allDay);
		CHECK(e.hasDuration && e.durationSeconds == 3600 && e.durationText == "PT1H");
		CHECK(e.location == "Karlsplatz 13, 1040 Wien, Austria");
		CHECK(e.description == "Quarterly planning.\nBring the numbers, please.\n\nJoin: https://meet.example.org/abc");
		CHECK(e.categories.size() == 2 && e.categories[0] == "Work" && e.categories[1] == "Planning");
		CHECK(e.hasOrganizer && e.organizer.name == "Max Muster" && e.organizer.email == "max@example.org" && e.organizer.role == "ORGANIZER");
		CHECK(e.attendees.size() == 4 && e.attendees[1].name == "Jane Roe" && e.attendees[1].email == "jane@example.org" && e.attendees[1].role == "REQ-PARTICIPANT" && e.attendees[1].status == "NEEDS-ACTION");
		CHECK(e.attendees[2].role == "OPT-PARTICIPANT" && e.attendees[2].status == "TENTATIVE" && e.attendees[3].type == "RESOURCE" && e.attendees[3].name == "Meeting Room 4");
		CHECK(!e.hasRule && cal.notes.empty());
		bool a, b;
		Occurrence p, n;
		CHECK(!SeriesNeighbours(cal, e, &a, &p, &b, &n));		// (no series)
	}

	// ---- Outlook: a Windows zone with its own rules, a series with an exception and a moved instance
	{
		Calendar cal = ReadCalendar(Read("outlook.ics"));
		CHECK(cal.method == "REQUEST" && cal.events.size() == 2 && cal.DistinctEvents().size() == 1);
		const Event& master = cal.events[0];
		CHECK(master.summary == "Weekly Sync" && master.hasRule && master.rule.count == 6 && master.exceptions.size() == 1);
		CHECK(master.startUnix == U(2026, 11, 2, 8, 0) && master.endUnix == U(2026, 11, 2, 9, 0));		// (winter time, +01:00)
		CHECK(master.organizer.name == "Muster, Max" && master.organizer.email == "max.muster@example.org" && master.attendees.size() == 1 && master.attendees[0].name == "Roe, Jane");
		CHECK(master.description == "Weekly sync\n\nAgenda:\n1. News\n2. Open points");		// (the blanks at the end are trimmed)
		// the changes of the clock, from the rules in the file (the last Sundays of March and October)
		int offset = 0;
		const std::string zone = "W. Europe Standard Time";
		CHECK(cal.zones.OffsetAt(zone, U(2026, 3, 29, 1, 59), &offset) && offset == 3600);
		CHECK(cal.zones.OffsetAt(zone, U(2026, 3, 29, 3, 30), &offset) && offset == 7200);
		CHECK(cal.zones.OffsetAt(zone, U(2026, 7, 15, 9, 30), &offset) && offset == 7200);
		CHECK(cal.zones.OffsetAt(zone, U(2026, 10, 25, 4, 0), &offset) && offset == 3600);
		CHECK(cal.zones.OffsetAt(zone, U(2027, 1, 1, 0, 0), &offset) && offset == 3600);
		CHECK(cal.zones.OffsetAt(zone, U(1999, 8, 1, 12, 0), &offset) && offset == 7200);
		// the series: the master has nothing before it; the instance of 30 November (moved to 1 December) has 23 November before it (the 16th is an exception), 7 December after
		bool hasPrev, hasNext;
		Occurrence prev, next;
		CHECK(SeriesNeighbours(cal, master, &hasPrev, &prev, &hasNext, &next) && !hasPrev && hasNext && next.start.day == 9 && next.startUnix == U(2026, 11, 9, 8, 0) && next.endUnix == U(2026, 11, 9, 9, 0));
		CHECK(OccurrenceKey(next.start) == "20261109T090000" && next.summary == "Weekly Sync" && !next.moved);
		const Event& moved = cal.events[1];
		CHECK(moved.hasRecurrenceId && moved.startUnix == U(2026, 12, 1, 9, 0));
		CHECK(SeriesNeighbours(cal, moved, &hasPrev, &prev, &hasNext, &next) && hasPrev && hasNext);
		CHECK(prev.startUnix == U(2026, 11, 23, 8, 0) && next.startUnix == U(2026, 12, 7, 8, 0) && next.start.month == 12 && next.start.day == 7);
		// ... and the master sees the moved instance as the one after the 23rd
		Event inBetween = master;
		inBetween.start = FromLocalSeconds(LocalSeconds(master.start) + 21 * 86400);	// (the 23rd, the slot before)
		inBetween.start.tzid = master.start.tzid;
		inBetween.hasRecurrenceId = true;
		inBetween.recurrenceId = inBetween.start;
		CHECK(SeriesNeighbours(cal, inBetween, &hasPrev, &prev, &hasNext, &next) && hasNext && next.moved && next.startUnix == U(2026, 12, 1, 9, 0) && next.summary == "Weekly Sync (moved)" && hasPrev && prev.startUnix == U(2026, 11, 9, 8, 0));
	}

	// ---- Apple: a zone of the database that the file does not define (the system's), a place, an address
	{
		Calendar cal = ReadCalendar(Read("apple.ics"));
		CHECK(cal.events.size() == 1);
		const Event& e = cal.events[0];
		CHECK(e.summary == "Visit" && e.uid == "7B2C-4F11" && e.url == "https://example.org/visit");
		CHECK(e.startUnix == U(2026, 11, 16, 14, 30) && e.endUnix == U(2026, 11, 16, 15, 30));		// (New York, -05:00)
		CHECK(e.hasGeo && e.latitude > 40.74 && e.latitude < 40.75 && e.longitude < -73.98 && e.longitude > -73.99);
		CHECK(e.location == "Empire State Building\n20 W 34th St\nNew York, NY 10001");
	}

	// ---- zones that the system answers for (a name that it does not know is none)
	{
		int offset = 0;
		CHECK(PosixZoneOffset("America/New_York", U(2026, 11, 16, 9, 30), &offset) && offset == -5 * 3600);
		CHECK(PosixZoneOffset("America/New_York", U(2026, 7, 16, 9, 30), &offset) && offset == -4 * 3600);
		CHECK(PosixZoneOffset("Europe/London", U(2026, 1, 16, 9, 30), &offset) && offset == 0);		// (an offset of 0 that is real)
		CHECK(PosixZoneOffset("Europe/London", U(2026, 7, 16, 9, 30), &offset) && offset == 3600);
		CHECK(PosixZoneOffset("UTC", U(2026, 7, 16, 9, 30), &offset) && offset == 0 && PosixZoneOffset("Etc/GMT", U(2026, 7, 16, 9, 30), &offset));
		CHECK(!PosixZoneOffset("Foo/Bar", U(2026, 7, 16, 9, 30), &offset) && !PosixZoneOffset("Nonsense", U(2026, 7, 16, 9, 30), &offset) && !PosixZoneOffset("Customized Time Zone", U(2026, 7, 16, 9, 30), &offset));
		// (an event in such a zone is taken as a time of this computer)
		Calendar odd = ReadCalendar("BEGIN:VEVENT\nSUMMARY:x\nDTSTART;TZID=Foo/Bar:20261116T093000\nEND:VEVENT\n");
		CHECK(odd.events[0].startKnown && odd.events[0].startUnix == U(2026, 11, 16, 8, 30));
	}

	// ---- days and durations (the zone of this computer is Vienna)
	{
		Calendar cal = ReadCalendar(Read("allday.ics"));
		CHECK(cal.events.size() == 3);
		const Event& christmas = cal.events[0];
		CHECK(christmas.allDay && christmas.startUnix == U(2026, 12, 23, 23, 0) && christmas.endUnix == U(2026, 12, 26, 23, 0) && christmas.durationText == "P3D" && christmas.transparency == "TRANSPARENT");
		const Event& birthday = cal.events[1];
		CHECK(birthday.allDay && birthday.hasEnd && birthday.endUnix - birthday.startUnix == 86400 && birthday.hasRule);
		bool hasPrev, hasNext;
		Occurrence prev, next;
		CHECK(SeriesNeighbours(cal, birthday, &hasPrev, &prev, &hasNext, &next) && !hasPrev && hasNext && next.start.year == 2027 && next.start.month == 11 && next.start.dateOnly);
		CHECK(OccurrenceKey(next.start) == "20271120");
		// as of a later moment (a calendar app shows the series from today): the next birthday after it, the last one before it
		{
			Occurrence prevNow, nextNow;
			bool hasPrevNow, hasNextNow;
			const int64_t today = LocalSeconds(FromLocalSeconds(DaysFromCivil(2030, 6, 15) * 86400));
			CHECK(SeriesNeighbours(cal, birthday, &hasPrevNow, &prevNow, &hasNextNow, &nextNow, today) && hasPrevNow && hasNextNow);
			CHECK(OccurrenceKey(prevNow.start) == "20291120" && OccurrenceKey(nextNow.start) == "20301120");
			// (an instant that is in the future of the series' own start is not moved back: as of a moment before it nothing changes)
			CHECK(SeriesNeighbours(cal, birthday, &hasPrevNow, &prevNow, &hasNextNow, &nextNow, 1) && !hasPrevNow && OccurrenceKey(nextNow.start) == "20271120");
		}
		const Event& workshop = cal.events[2];
		CHECK(workshop.hasDuration && workshop.durationSeconds == 9000 && workshop.endKnown && workshop.endUnix == U(2026, 12, 1, 11, 30));
	}

	// ---- vCalendar 1.0
	{
		Calendar cal = ReadCalendar(Read("old.vcs"));
		CHECK(cal.IsOldVersion() && cal.events.size() == 1);
		const Event& e = cal.events[0];
		CHECK(e.summary == "Zahnarzt K\xC3\xB6nig" && e.location == "Hauptstra\xC3\x9F" "e 5");
		CHECK(e.description == "Kontrolle\r\nBitte Versicherungskarte mitbringen");
		CHECK(e.startUnix == U(2026, 11, 18, 14, 0) && e.hasRule && e.rule.frequency == Frequency::Weekly && e.rule.count == 3);
		CHECK(e.attendees.size() == 2 && e.attendees[0].name == "Hans Meier" && e.attendees[0].email == "hans@example.org" && e.attendees[0].status == "ACCEPTED");
		CHECK(e.attendees[1].name == "Anna Schmidt" && e.attendees[1].email.empty() && e.attendees[1].status == "NEEDS ACTION");
		bool hasPrev, hasNext;
		Occurrence prev, next;
		CHECK(SeriesNeighbours(cal, e, &hasPrev, &prev, &hasNext, &next) && !hasPrev && hasNext && next.startUnix == U(2026, 11, 25, 14, 0));
	}

	// ---- an export with several events
	{
		Calendar cal = ReadCalendar(Read("multi.ics"));
		CHECK(cal.name == "Holidays" && cal.DistinctEvents().size() == 3 && cal.events[1].summary == "Epiphany");
		CHECK(cal.description == "Public holidays");
	}

	// ---- damage: nothing fails, what cannot be read is left out and told
	{
		Calendar cal = ReadCalendar(Read("broken.ics"));
		CHECK(cal.events.size() == 2 && !cal.notes.empty());
		CHECK(!cal.events[0].hasStart && cal.events[0].summary == "Broken date");
		const Event& second = cal.events[1];
		CHECK(second.summary == "Second" && second.hasStart && !second.hasEnd && !second.hasRule);		// (the end is before the start: left out)
		CHECK(cal.DistinctEvents().size() == 2);
		Calendar empty = ReadCalendar("");
		CHECK(empty.events.empty());
		Calendar text = ReadCalendar("just some text\nBEGIN:VEVENT\nSUMMARY:x\n");
		CHECK(text.events.size() == 1 && text.events[0].summary == "x" && !text.events[0].hasStart);
	}

	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
