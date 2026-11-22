/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
// Plain C++ test of the lexer of calendar files.
#include "../core/IcsParser.h"

#include <stdio.h>

using namespace ics;

static int sChecks = 0, sFailures = 0;
#define CHECK(x) do { sChecks++; if (!(x)) { sFailures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int main()
{
	// a plain calendar, CRLF, a folded line, escapes
	Parsed p = Parse("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//x//EN\r\nBEGIN:VEVENT\r\nUID:1@x\r\nSUMMARY:Planning\\, part\r\n  one\r\nDESCRIPTION:Line 1\\nLine 2\r\n LONG\r\nDTSTART;TZID=Europe/Vienna:20261116T093000\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
	CHECK(p.components.size() == 1 && p.components[0].name == "VCALENDAR" && p.notes.empty());
	const Component& cal = p.components[0];
	CHECK(cal.Find("VERSION") != NULL && cal.Find("VERSION")->value == "2.0");
	std::vector<const Component*> events = cal.Children("VEVENT");
	CHECK(events.size() == 1);
	const Component& event = *events[0];
	CHECK(UnescapeText(event.Find("SUMMARY")->value) == "Planning, part one");		// (the fold removes one blank: the second stays)
	CHECK(UnescapeText(event.Find("DESCRIPTION")->value) == "Line 1\nLine 2LONG");
	const Property* start = event.Find("DTSTART");
	CHECK(start != NULL && start->value == "20261116T093000" && start->ParamValue("tzid") != NULL && *start->ParamValue("TZID") == "Europe/Vienna");

	// LF only, a byte order mark, lower case, a group, no closing lines
	p = Parse("\xEF\xBB\xBF" "begin:vcalendar\nbegin:vevent\nsummary:Lunch\nitem1.x-note:n\nBEGIN:VALARM\nACTION:DISPLAY\n");
	CHECK(p.components.size() == 1 && p.components[0].name == "VCALENDAR");
	CHECK(p.components[0].Children("VEVENT").size() == 1 && p.components[0].Children("VEVENT")[0]->Find("SUMMARY")->value == "Lunch");
	CHECK(p.components[0].Children("VEVENT")[0]->Find("X-NOTE") != NULL && p.components[0].Children("VEVENT")[0]->Children("VALARM").size() == 1);
	CHECK(p.notes.size() == 3);		// (three components that were not closed)

	// an event that is not closed is not the parent of the next one
	p = Parse("BEGIN:VCALENDAR\nBEGIN:VEVENT\nSUMMARY:One\nBEGIN:VEVENT\nSUMMARY:Two\nEND:VEVENT\nEND:VCALENDAR\n");
	CHECK(p.components.size() == 1 && p.components[0].Children("VEVENT").size() == 2 && p.components[0].Children("VEVENT")[1]->Find("SUMMARY")->value == "Two" && p.notes.size() == 1);

	// CR only (old Mac)
	p = Parse("BEGIN:VCALENDAR\rBEGIN:VEVENT\rSUMMARY:Old\rEND:VEVENT\rEND:VCALENDAR\r");
	CHECK(p.components.size() == 1 && p.components[0].Children("VEVENT").size() == 1);

	// a loose event with no calendar; lines without a colon, an END that matches nothing, a property outside
	p = Parse("junk\nSTRAY:x\nBEGIN:VEVENT\nSUMMARY:Loose\nnothing here\nEND:VTODO\nEND:VEVENT\n");
	CHECK(p.components.size() == 1 && p.components[0].name == "VEVENT" && p.components[0].Find("SUMMARY")->value == "Loose" && !p.notes.empty());

	// quoted parameter values with colon and semicolon, several values
	p = Parse("BEGIN:VEVENT\nATTENDEE;CN=\"Doe; John: Jr.\";ROLE=REQ-PARTICIPANT;MEMBER=\"mailto:a@x\",\"mailto:b@x\":mailto:john@example.org\nEND:VEVENT\n");
	const Property* attendee = p.components[0].Find("ATTENDEE");
	CHECK(attendee != NULL && attendee->value == "mailto:john@example.org" && *attendee->ParamValue("CN") == "Doe; John: Jr." && *attendee->ParamValue("ROLE") == "REQ-PARTICIPANT");
	CHECK(attendee->params.size() == 3 && attendee->params[2].values.size() == 2 && attendee->params[2].values[1] == "mailto:b@x");

	// vCalendar 1.0: quoted-printable over lines, bare parameters, a character set
	p = Parse("BEGIN:VCALENDAR\nVERSION:1.0\nBEGIN:VEVENT\nSUMMARY;ENCODING=QUOTED-PRINTABLE:Caf=C3=A9 =\n au lait\nDESCRIPTION;QUOTED-PRINTABLE;CHARSET=ISO-8859-1:M=FCnchen=0D=0Abr=FCcke\nEND:VEVENT\nEND:VCALENDAR\n");
	const Component& vevent = *p.components[0].Children("VEVENT")[0];
	CHECK(vevent.Find("SUMMARY")->value == "Caf\xC3\xA9  au lait");		// (the soft line break takes out the = and the line end only)
	CHECK(vevent.Find("DESCRIPTION")->value == "M\xC3\xBCnchen\r\nbr\xC3\xBC" "cke");

	// text that is not UTF-8 is Windows-1252
	p = Parse("BEGIN:VEVENT\nSUMMARY:Se\xF1or \x80 5\nEND:VEVENT\n");
	CHECK(p.components[0].Find("SUMMARY")->value == "Se\xC3\xB1or \xE2\x82\xAC 5");
	// ... and UTF-8 stays
	p = Parse("BEGIN:VEVENT\nSUMMARY:\xC3\x9C" "bung\nEND:VEVENT\n");
	CHECK(p.components[0].Find("SUMMARY")->value == "\xC3\x9C" "bung");

	// helpers
	CHECK(UnescapeText("a\\,b\\;c\\\\d\\Ne") == "a,b;c\\d\ne");
	std::vector<std::string> parts = SplitEscaped("a\\,b,c,,d", ',');
	CHECK(parts.size() == 4 && parts[0] == "a\\,b" && parts[2].empty());
	CHECK(LooksLikeCalendar("BEGIN:VCALENDAR\r\n") && LooksLikeCalendar("\xEF\xBB\xBF" "\n\nbegin:vevent") && !LooksLikeCalendar("hello BEGIN:VCALENDAR") && !LooksLikeCalendar("BEGIN:VCARD\n") && !LooksLikeCalendar(""));
	CHECK(IsUtf8("\xC3\xA4") && !IsUtf8("\xE4") && !IsUtf8("\xC3") && IsUtf8("abc"));

	// nothing at all
	p = Parse("");
	CHECK(p.components.empty());
	p = Parse("random text without structure");
	CHECK(p.components.empty());

	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
