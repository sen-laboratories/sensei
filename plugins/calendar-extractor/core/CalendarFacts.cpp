/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "CalendarFacts.h"

#include <stdio.h>
#include <strings.h>

#include <algorithm>

#include "IcsText.h"

namespace calendar {

namespace {

std::string
Upper(const std::string& text)
{
	std::string out = text;
	for (char& c : out)
		c = (char)toupper((unsigned char)c);
	return out;
}

std::string
DayText(const ics::DateTime& time)
{
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", time.year, time.month, time.day);
	return buffer;
}

std::string
FloatingText(const ics::DateTime& time)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d", time.year, time.month, time.day, time.hour, time.minute, time.second);
	return buffer;
}

/** whether a text is only a link (a location that is the address of the meeting) */
bool
IsOnlyLink(const std::string& text)
{
	const std::vector<std::string> urls = ics::FindUrls(text);
	if (urls.size() != 1)
		return false;
	std::string rest = text;
	const size_t at = rest.find(urls[0]);
	if (at == std::string::npos)
		return false;
	rest.erase(at, urls[0].size());
	for (char c : rest) {
		if (!isspace((unsigned char)c) && c != '<' && c != '>' && c != '(' && c != ')')
			return false;
	}
	return true;
}

}	// namespace


std::string
NaturalName(const std::string& input)
{
	std::string name = input;
	auto trim = [](std::string text) {
		size_t first = 0, last = text.size();
		while (first < last && (isspace((unsigned char)text[first]) || text[first] == '"'))
			first++;
		while (last > first && (isspace((unsigned char)text[last - 1]) || text[last - 1] == '"'))
			last--;
		return text.substr(first, last - first);
	};
	name = trim(name);
	const size_t comma = name.find(',');
	if (comma != std::string::npos && name.find(',', comma + 1) == std::string::npos && name.find('@') == std::string::npos) {
		const std::string family = trim(name.substr(0, comma)), given = trim(name.substr(comma + 1));
		if (!family.empty() && !given.empty())
			return given + " " + family;
	}
	return name;
}


std::string
PersonText(const ics::Person& person)
{
	const std::string name = NaturalName(person.name);
	if (!name.empty() && !person.email.empty())
		return name + " <" + person.email + ">";
	return name.empty() ? person.email : name;
}


std::string
EventStatusText(const std::string& status)
{
	const std::string value = Upper(status);
	if (value == "CONFIRMED" || value == "ACCEPTED")
		return "Confirmed";
	if (value == "TENTATIVE")
		return "Tentative";
	if (value == "CANCELLED" || value == "CANCELED")
		return "Cancelled";
	return std::string();
}


std::string
ResponseText(const std::string& partstat)
{
	const std::string value = Upper(partstat);
	if (value == "ACCEPTED" || value == "CONFIRMED")
		return "Accepted";
	if (value == "DECLINED")
		return "Declined";
	if (value == "TENTATIVE")
		return "Tentative";
	if (value == "NEEDS-ACTION" || value == "NEEDS ACTION" || value == "SENT")
		return "Needs action";
	if (value == "DELEGATED")
		return "Delegated";
	return std::string();
}


std::string
RoleText(const ics::Person& person)
{
	const std::string role = Upper(person.role);
	if (role == "ORGANIZER")
		return "organizer";
	if (role == "CHAIR")
		return "chair";
	if (role == "OPT-PARTICIPANT")
		return "optional";
	if (role == "NON-PARTICIPANT")
		return person.type == "RESOURCE" || person.type == "ROOM" ? "resource" : "informed";
	if (person.type == "RESOURCE" || person.type == "ROOM")
		return "resource";
	return "required";
}


Facts
FactsOf(const ics::Calendar& /*calendar: the times of the event are worked out already*/, const ics::Event& event, const std::string& namePattern)
{
	Facts facts;
	facts.title = event.summary;
	facts.uid = event.uid;
	facts.location = event.location;
	facts.url = event.url;
	facts.recurrence = event.ruleText;
	{
		ics::Rule rule;
		if (!event.ruleText.empty() && ics::ParseRule(event.ruleText, &rule))
			facts.repeats = ics::DescribeRule(rule);
	}
	facts.status = EventStatusText(event.status);
	if (event.hasOrganizer)
		facts.organizer = PersonText(event.organizer);
	{
		// the people as a list that can be read at a glance: the organizer first, each once (by address, else by name), with the role and the answer in brackets
		ics::Person organizer = event.organizer;
		if (event.hasOrganizer) {
			// (the organizer is mostly an attendee as well, and it is there that the answer to the invitation is)
			for (const ics::Person& person : event.attendees) {
				if (!organizer.email.empty() && strcasecmp(person.email.c_str(), organizer.email.c_str()) == 0 && organizer.status.empty())
					organizer.status = person.status;
			}
		}
		std::vector<const ics::Person*> people;
		if (event.hasOrganizer)
			people.push_back(&organizer);
		for (const ics::Person& person : event.attendees)
			people.push_back(&person);
		std::vector<std::string> seen;
		for (const ics::Person* person : people) {
			std::string text = PersonText(*person);
			if (text.empty())
				continue;
			const std::string key = person->email.empty() ? text : person->email;
			bool known = false;
			for (const std::string& each : seen)
				known = known || strcasecmp(each.c_str(), key.c_str()) == 0;
			if (known)
				continue;
			seen.push_back(key);
			std::string details = RoleText(*person);
			const std::string answer = ResponseText(person->status);
			if (!answer.empty())
				details += (details.empty() ? "" : ", ") + answer;
			if (!details.empty())
				text += " (" + details + ")";
			std::replace(text.begin(), text.end(), ';', ',');		// (the separator of the list)
			facts.attendees.push_back(text);
		}
	}

	facts.markdown = ics::DescriptionToMarkdown(event.description, event.htmlDescription);
	facts.summary = ics::PlainSummary(facts.markdown, 300);

	// the times: seconds since 1970 (UTC). A day and a time that no zone says mean the zone of this computer, and their civil text is kept too
	facts.allDay = event.allDay;
	if (event.hasStart && event.startKnown) {
		facts.hasStart = true;
		facts.startUnix = event.startUnix;
		if (event.endKnown && event.hasEnd) {
			facts.hasEnd = true;
			facts.endUnix = event.endUnix;
		} else if (event.hasDuration) {
			facts.duration = event.durationText;
		}
		if (event.allDay) {
			facts.startEdtf = DayText(event.start);
			// (the end of a day is the day after: the last day is the one before it)
			const ics::DateTime last = event.hasEnd ? ics::FromLocalSeconds(ics::LocalSeconds(event.end) - 86400) : event.start;
			facts.endEdtf = DayText(event.hasEnd && ics::LocalSeconds(event.end) > ics::LocalSeconds(event.start) ? last : event.start);
		} else if (!event.start.utc && event.start.tzid.empty()) {
			facts.startEdtf = FloatingText(event.start);
			if (event.hasEnd && !event.end.utc && event.end.tzid.empty())
				facts.endEdtf = FloatingText(event.end);
		}
		facts.localStart = event.allDay ? event.start : ics::UnixToLocal(event.startUnix);
		facts.baseName = ics::BuildEventName(namePattern, facts.localStart, !event.allDay, event.summary);
	}

	// the links: the conference properties first, then the location, the page of the event and the description (of the text as Markdown, which has the links of an HTML text too)
	std::vector<std::string> links = event.conferenceUrls;
	for (const std::string& url : ics::FindUrls(event.location))
		links.push_back(url);
	if (!event.url.empty())
		links.push_back(event.url);
	for (const std::string& url : ics::FindUrls(facts.markdown))
		links.push_back(url);
	for (const std::string& url : links) {
		if (std::find(facts.links.begin(), facts.links.end(), url) == facts.links.end())
			facts.links.push_back(url);
	}
	// the first link of a known service is the meeting; a conference property that no service is known for is a meeting of another one
	for (const std::string& url : facts.links) {
		std::string service;
		if (ics::ClassifyMeetingUrl(url, &service)) {
			facts.meetingUrl = url;
			facts.meetingType = service;
			break;
		}
	}
	if (facts.meetingUrl.empty() && !event.conferenceUrls.empty()) {
		facts.meetingUrl = event.conferenceUrls[0];
		facts.meetingType = "Other";
	}
	if (!facts.meetingUrl.empty())
		facts.attendanceMode = (event.location.empty() || IsOnlyLink(event.location) || event.location == facts.meetingUrl) ? "Online" : "Mixed";
	else if (!event.location.empty())
		facts.attendanceMode = "Offline";
	return facts;
}

}	// namespace calendar
