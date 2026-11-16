/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
// Plain C++ test of what is written about an event (the files of tests/fixtures; the folder is FIXTURES).
#include "../core/CalendarFacts.h"

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <sstream>

using namespace ics;
using namespace calendar;

static int sChecks = 0, sFailures = 0;
#define CHECK(x) do { sChecks++; if (!(x)) { sFailures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
#define CHECK_EQ(a, b) do { sChecks++; const std::string va = (a), vb = (b); if (va != vb) { sFailures++; printf("FAIL %s:%d: %s\n  got:  [%s]\n  want: [%s]\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); } } while (0)

static std::string
Read(const char* name)
{
	std::ifstream in(std::string(FIXTURES) + "/" + name, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

static int64_t
U(int y, int m, int d, int hh = 0, int mm = 0)
{
	return DaysFromCivil(y, m, d) * 86400 + hh * 3600 + mm * 60;
}

int main()
{
	setenv("TZ", "Europe/Vienna", 1);
	tzset();

	// Google: times, status, people, place, a file name in the zone of this computer (UTC+1 in November)
	{
		Calendar cal = ReadCalendar(Read("google.ics"));
		const Facts f = FactsOf(cal, cal.events[0]);
		CHECK(f.hasStart && f.hasEnd && f.startUnix == U(2026, 11, 16, 8, 30) && f.endUnix == U(2026, 11, 16, 9, 30) && f.startEdtf.empty() && f.endEdtf.empty() && !f.allDay);
		CHECK_EQ(f.baseName, "2026-11-16_0930_Planning");
		CHECK_EQ(f.status, "Confirmed");
		CHECK_EQ(f.organizer, "Max Muster <max@example.org>");
		CHECK_EQ(f.attendanceMode, "Offline");
		CHECK(f.meetingUrl.empty() && f.meetingType.empty());
		CHECK_EQ(f.summary, "Quarterly planning. Bring the numbers, please. Join: https://meet.example.org/abc");
		CHECK_EQ(f.markdown, "Quarterly planning.  \nBring the numbers, please.\n\nJoin: <https://meet.example.org/abc>\n");
		CHECK_EQ(f.uid, "abc123@google.com");
		CHECK(f.links.size() == 1 && f.links[0] == "https://meet.example.org/abc");
		CHECK(PersonText(cal.events[0].attendees[1]) == "Jane Roe <jane@example.org>" && RoleText(cal.events[0].attendees[2]) == "optional" && RoleText(cal.events[0].attendees[3]) == "resource"
			&& RoleText(cal.events[0].organizer) == "organizer" && RoleText(cal.events[0].attendees[1]) == "required");
		CHECK(ResponseText(cal.events[0].attendees[0].status) == "Accepted" && ResponseText(cal.events[0].attendees[1].status) == "Needs action" && ResponseText(cal.events[0].attendees[2].status) == "Tentative");
	}

	// days and floating times keep their civil text
	{
		Calendar cal = ReadCalendar(Read("allday.ics"));
		const Facts christmas = FactsOf(cal, cal.events[0]);
		CHECK(christmas.allDay && christmas.startEdtf == "2026-12-24" && christmas.endEdtf == "2026-12-26" && christmas.hasEnd);
		CHECK_EQ(christmas.baseName, "2026-12-24_Christmas");
		const Facts birthday = FactsOf(cal, cal.events[1]);
		CHECK(birthday.startEdtf == "2026-11-20" && birthday.endEdtf == "2026-11-20" && birthday.recurrence == "FREQ=YEARLY");
		const Facts workshop = FactsOf(cal, cal.events[2]);
		CHECK_EQ(workshop.baseName, "2026-12-01_1000_Workshop");
		CHECK(workshop.startEdtf.empty());
		Calendar floating = ReadCalendar("BEGIN:VEVENT\nSUMMARY:Local\nDTSTART:20261116T093000\nDTEND:20261116T103000\nEND:VEVENT\n");
		const Facts local = FactsOf(floating, floating.events[0]);
		CHECK(local.startEdtf == "2026-11-16T09:30:00" && local.endEdtf == "2026-11-16T10:30:00" && local.startUnix == U(2026, 11, 16, 8, 30));
	}

	// meetings: the conference property, the description, the location, a service that is not known
	{
		Calendar cal = ReadCalendar(Read("meetings.ics"));
		CHECK(cal.events.size() == 5);
		const Facts design = FactsOf(cal, cal.events[0]);
		CHECK(design.meetingUrl == "https://meet.google.com/abc-defg-hij" && design.meetingType == "Google Meet" && design.attendanceMode == "Mixed");
		CHECK(design.links.size() == 3 && design.links[1] == "https://docs.example.org/notes" && design.links[2] == "https://zoom.us/j/555");		// (the conference property, then the description)
		const Facts teams = FactsOf(cal, cal.events[1]);
		CHECK(teams.meetingType == "Microsoft Teams" && teams.meetingUrl == "https://teams.microsoft.com/l/meetup-join/19%3ameeting_abc" && teams.attendanceMode == "Mixed");
		CHECK_EQ(teams.markdown, "Hello **all**,\n\nJoin [the meeting](https://teams.microsoft.com/l/meetup-join/19%3ameeting_abc).\n");
		CHECK_EQ(teams.summary, "Hello all, Join the meeting.");
		const Facts zoom = FactsOf(cal, cal.events[2]);
		CHECK(zoom.meetingType == "Zoom" && zoom.attendanceMode == "Online" && zoom.url == "https://example.org/event" && zoom.links.size() == 2);		// (the link of the location and of the description is the same)
		const Facts other = FactsOf(cal, cal.events[3]);
		CHECK(other.meetingType == "Other" && other.meetingUrl == "https://conf.example.org/room/7" && other.attendanceMode == "Online");
		const Facts lunch = FactsOf(cal, cal.events[4]);
		CHECK(lunch.status == "Cancelled" && lunch.attendanceMode == "Offline" && lunch.meetingUrl.empty() && lunch.markdown.empty() && lunch.summary.empty());
	}

	// vCalendar 1.0: the status of the owner
	{
		Calendar cal = ReadCalendar(Read("old.vcs"));
		const Facts f = FactsOf(cal, cal.events[0]);
		CHECK(f.status == "Confirmed" && f.title == "Zahnarzt K\xC3\xB6nig");
		CHECK(ResponseText("NEEDS ACTION") == "Needs action" && ResponseText("ACCEPTED") == "Accepted" && ResponseText("") == "" && EventStatusText("NEEDS ACTION") == "");
	}

	CHECK_EQ(NaturalName("Roe, Jane"), "Jane Roe");
	CHECK_EQ(NaturalName("  \"Muster, Max\" "), "Max Muster");
	CHECK_EQ(NaturalName("Jane Roe"), "Jane Roe");
	CHECK_EQ(NaturalName("Acme, Inc., Vienna"), "Acme, Inc., Vienna");
	CHECK_EQ(NaturalName(""), "");

	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
