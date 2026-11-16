/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
// Plain C++ test of the text of events: Markdown, links, meetings, file names.
#include "../core/IcsText.h"

#include <stdio.h>

using namespace ics;

static int sChecks = 0, sFailures = 0;
#define CHECK(x) do { sChecks++; if (!(x)) { sFailures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
#define CHECK_EQ(a, b) do { sChecks++; const std::string va = (a), vb = (b); if (va != vb) { sFailures++; printf("FAIL %s:%d: %s\n  got:  [%s]\n  want: [%s]\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); } } while (0)

int main()
{
	// plain text: lines stay lines, a link is a link, marks are escaped
	CHECK_EQ(DescriptionToMarkdown("Quarterly planning.\nBring the numbers, please.\n\nJoin: https://meet.example.org/abc.", ""),
		"Quarterly planning.  \nBring the numbers, please.\n\nJoin: <https://meet.example.org/abc>.\n");
	CHECK_EQ(DescriptionToMarkdown("a * b _c_ `d`\n# not a heading\n- not a list", ""), "a \\* b \\_c\\_ \\`d\\`  \n\\# not a heading  \n\\- not a list\n");
	CHECK_EQ(DescriptionToMarkdown("\n\n  \nText\n\n\n\nMore\n\n", ""), "Text\n\nMore\n");
	CHECK_EQ(DescriptionToMarkdown("", ""), "");

	// HTML of Google (tags in the description) and Outlook (the alternative text)
	CHECK_EQ(DescriptionToMarkdown("Agenda:<br><ul><li>News</li><li>Open <b>points</b></li></ul>Join <a href=\"https://zoom.us/j/123\">the meeting</a> or <a href=\"https://example.org\">https://example.org</a>.", ""),
		"Agenda:  \n\n- News\n- Open **points**\n\nJoin [the meeting](https://zoom.us/j/123) or <https://example.org>.\n");
	CHECK_EQ(DescriptionToMarkdown("ignored", "<html><head><style>p {color: red}</style></head><body><p>Hello&nbsp;<i>world</i> &amp; friends</p><p>Second<br>line</p></body></html>"),
		"Hello *world* & friends\n\nSecond  \nline\n");
	CHECK_EQ(HtmlToMarkdown("<h2>Title</h2><ol><li>one</li><li>two<ul><li>inner</li></ul></li></ol><p>x</p>"), "## Title\n\n1. one\n2. two\n    - inner\n\nx\n");
	CHECK_EQ(HtmlToMarkdown("Caf&eacute; &#8217;s &#x41;"), "Caf&eacute; \xE2\x80\x99s A\n");
	CHECK(LooksLikeHtml("a<br>b") && LooksLikeHtml("<a href='x'>y</a>") && !LooksLikeHtml("1 < 2 and 3 > 2") && !LooksLikeHtml("plain"));

	// links
	std::vector<std::string> urls = FindUrls("See https://example.org/a, (https://example.org/b) and <https://example.org/c>. [x](https://example.org/d) http://old.example.org/e?x=1&y=2.");
	CHECK(urls.size() == 5 && urls[0] == "https://example.org/a" && urls[1] == "https://example.org/b" && urls[2] == "https://example.org/c" && urls[3] == "https://example.org/d" && urls[4] == "http://old.example.org/e?x=1&y=2");
	CHECK(FindUrls("no links here").empty() && FindUrls("twice https://example.org/z https://example.org/z").size() == 1);
	CHECK(FindUrls("<a href=\"https://example.org/h?a=1&amp;b=2\">x</a>").size() == 1 && FindUrls("<a href=\"https://example.org/h?a=1&amp;b=2\">x</a>")[0] == "https://example.org/h?a=1&b=2");

	// meetings
	std::string service;
	CHECK(ClassifyMeetingUrl("https://us02web.zoom.us/j/84721?pwd=abc", &service) && service == "Zoom");
	CHECK(ClassifyMeetingUrl("https://zoom.us/my/someone", &service) && service == "Zoom");
	CHECK(ClassifyMeetingUrl("https://meet.google.com/abc-defg-hij", &service) && service == "Google Meet");
	CHECK(ClassifyMeetingUrl("https://teams.microsoft.com/l/meetup-join/19%3ameeting", &service) && service == "Microsoft Teams");
	CHECK(ClassifyMeetingUrl("https://example.webex.com/meet/someone", &service) && service == "Webex");
	CHECK(ClassifyMeetingUrl("https://meet.jit.si/Planning", &service) && service == "Jitsi");
	CHECK(ClassifyMeetingUrl("https://global.gotomeeting.com/join/123", &service) && service == "GoToMeeting");
	CHECK(ClassifyMeetingUrl("https://whereby.com/room", &service) && service == "Whereby");
	CHECK(ClassifyMeetingUrl("https://join.skype.com/abc", &service) && service == "Skype");
	CHECK(ClassifyMeetingUrl("HTTPS://ZOOM.US/j/1", &service) && service == "Zoom");
	CHECK(!ClassifyMeetingUrl("https://example.org/zoom.us/j/1", &service) && !ClassifyMeetingUrl("https://notzoom.us/j/1", &service) && !ClassifyMeetingUrl("https://zoom.us.evil.example/j/1", &service) && !ClassifyMeetingUrl("zoom.us", &service));

	// summary for a column
	CHECK_EQ(PlainSummary("# Title\n\nSome **bold** text with a [link](https://example.org) and <https://example.org/x>.\n\nSecond paragraph.", 200), "Title Some bold text with a link and https://example.org/x. Second paragraph.");
	CHECK_EQ(PlainSummary("one two three four five six seven eight nine ten eleven twelve", 30), "one two three four five six...");
	CHECK_EQ(PlainSummary("short", 30), "short");

	// names of files
	DateTime t;
	t.year = 2026; t.month = 11; t.day = 12; t.hour = 9; t.minute = 5;
	CHECK_EQ(EventBaseName(t, true, "Event Title"), "2026-11-12_0905_Event-Title");
	CHECK_EQ(EventBaseName(t, false, "Christmas"), "2026-11-12_Christmas");
	CHECK_EQ(EventBaseName(t, true, "  Planning: Q4 / budget?  "), "2026-11-12_0905_Planning-Q4-budget");
	CHECK_EQ(EventBaseName(t, true, ""), "2026-11-12_0905_Event");
	CHECK_EQ(EventBaseName(t, true, "K\xC3\xB6nig - Zahnarzt"), "2026-11-12_0905_K\xC3\xB6nig---Zahnarzt");
	CHECK(EventBaseName(t, true, std::string(200, 'a')).size() == std::string("2026-11-12_0905_").size() + 80);
	CHECK_EQ(EventBaseName(t, true, "...hidden"), "2026-11-12_0905_hidden");

	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
