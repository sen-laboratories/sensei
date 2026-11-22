/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
// Plain C++ test of the calendar text that is made of one event (the files of tests/fixtures; the folder is FIXTURES).
#include "../core/IcsWrite.h"
#include "../core/IcsCalendar.h"

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <sstream>

using namespace ics;

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

int main()
{
	setenv("TZ", "Europe/Vienna", 1);
	tzset();

	// every event of a calendar of several, one by one: a file with one event that reads as the event did
	for (const char* name : {"multi.ics", "meetings.ics", "allday.ics", "outlook.ics"}) {
		const std::string text = Read(name);
		const Calendar whole = ReadCalendar(text);
		const Parsed parsed = Parse(text);
		for (const Event* event : whole.DistinctEvents()) {
			const std::string one = EventFile(parsed, whole, EventKey(*event));
			CHECK(!one.empty());
			const Calendar again = ReadCalendar(one);
			const std::vector<const Event*> events = again.DistinctEvents();
			CHECK(events.size() == 1);
			if (events.size() != 1)
				continue;
			const Event& e = *events[0];
			CHECK_EQ(e.uid, event->uid);
			CHECK_EQ(e.summary, event->summary);
			CHECK_EQ(e.location, event->location);
			CHECK_EQ(e.description, event->description);
			CHECK(e.startKnown == event->startKnown && e.endKnown == event->endKnown);
			CHECK(e.startUnix == event->startUnix && e.endUnix == event->endUnix);
			CHECK(e.allDay == event->allDay);
			CHECK(e.attendees.size() == event->attendees.size());
			CHECK(e.hasRule == event->hasRule);
			// the lines are folded and end in CRLF
			size_t start = 0;
			while (start < one.size()) {
				const size_t end = one.find("\r\n", start);
				CHECK(end != std::string::npos && end - start <= 75);
				if (end == std::string::npos)
					break;
				start = end + 2;
			}
		}
	}

	// an event with a zone brings the zone, and not the others
	{
		const std::string text = Read("outlook.ics");
		const Calendar whole = ReadCalendar(text);
		const std::string one = EventFile(Parse(text), whole, EventKey(whole.events[0]));
		CHECK(one.find("BEGIN:VTIMEZONE") != std::string::npos);
		CHECK(one.find("BEGIN:VEVENT") != std::string::npos);
		CHECK(one.compare(0, 15, "BEGIN:VCALENDAR") == 0);
	}

	// a file of two events brings only the one that was asked for
	{
		const std::string text = Read("multi.ics");
		const Calendar whole = ReadCalendar(text);
		CHECK(whole.DistinctEvents().size() >= 2);
		const std::string one = EventFile(Parse(text), whole, EventKey(whole.events[0]));
		CHECK(one.find("BEGIN:VEVENT") == one.rfind("BEGIN:VEVENT"));
		CHECK(EventFile(Parse(text), whole, "no-such-uid").empty());
	}

	// vCalendar 1.0 (quoted-printable, a line break in a value): plain text, the title is as it was read
	{
		const std::string text = Read("old.vcs");
		const Calendar whole = ReadCalendar(text);
		const std::string key = EventKey(whole.events[0]);
		CHECK(whole.events[0].uid.empty() && key.compare(0, 4, "sen-") == 0);
		const std::string one = EventFile(Parse(text), whole, key);
		CHECK(!one.empty() && one.find("QUOTED-PRINTABLE") == std::string::npos);
		const Calendar again = ReadCalendar(one);
		CHECK(again.events.size() == 1);
		if (again.events.size() == 1) {
			CHECK_EQ(again.events[0].summary, whole.events[0].summary);
			CHECK_EQ(again.events[0].uid, key);		// (the file of its own has the key as its UID)
			CHECK_EQ(EventKey(again.events[0]), key);
		}
	}

	// long lines: folded after 75 bytes, never inside a character, and read as they were
	{
		std::string long_text = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:long-1\r\nDTSTART:20261101T100000Z\r\nSUMMARY:";
		for (int i = 0; i < 40; i++)
			long_text += "Gr\xC3\xBC\xC3\x9F" "e ";
		long_text += "\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
		const Calendar whole = ReadCalendar(long_text);
		const std::string one = EventFile(Parse(long_text), whole, "long-1");
		CHECK_EQ(ReadCalendar(one).events[0].summary, whole.events[0].summary);
		CHECK(one.find("\r\n ") != std::string::npos);
	}

	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
