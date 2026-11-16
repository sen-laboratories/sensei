/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "IcsCalendar.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>

namespace ics {

namespace {

std::string
Trimmed(const std::string& text)
{
	size_t first = 0, last = text.size();
	while (first < last && isspace((unsigned char)text[first]))
		first++;
	while (last > first && isspace((unsigned char)text[last - 1]))
		last--;
	return text.substr(first, last - first);
}

std::string
Upper(const std::string& text)
{
	std::string out = text;
	for (char& c : out)
		c = (char)toupper((unsigned char)c);
	return out;
}

int
OffsetSeconds(const std::string& text)
{
	// `+0100`, `-0430`, `+010000`
	const std::string value = Trimmed(text);
	if (value.size() < 5 || (value[0] != '+' && value[0] != '-'))
		return 0;
	const int hours = atoi(value.substr(1, 2).c_str());
	const int minutes = atoi(value.substr(3, 2).c_str());
	const int seconds = value.size() >= 7 ? atoi(value.substr(5, 2).c_str()) : 0;
	const int total = hours * 3600 + minutes * 60 + seconds;
	return value[0] == '-' ? -total : total;
}

}	// namespace


// ---- zones

void
TimeZones::Add(const Component& timezone)
{
	const Property* id = timezone.Find("TZID");
	if (id == NULL)
		return;
	const std::string tzid = Trimmed(id->value);
	std::vector<Observance> observances;
	for (const Component& child : timezone.children) {
		if (child.name != "STANDARD" && child.name != "DAYLIGHT")
			continue;
		const Property* start = child.Find("DTSTART");
		const Property* to = child.Find("TZOFFSETTO");
		if (start == NULL || to == NULL)
			continue;
		Observance observance;
		if (!ParseDateTime(start->value, "", false, &observance.onset))
			continue;
		observance.offsetTo = OffsetSeconds(to->value);
		const Property* from = child.Find("TZOFFSETFROM");
		observance.offsetFrom = from != NULL ? OffsetSeconds(from->value) : observance.offsetTo;
		const Property* rule = child.Find("RRULE");
		if (rule != NULL && ParseRule(rule->value, &observance.rule))
			observance.hasRule = true;
		for (const Property* extra : child.FindAll("RDATE")) {
			for (const std::string& each : SplitEscaped(extra->value, ',')) {
				DateTime time;
				if (ParseDateTime(each, "", false, &time))
					observance.extra.push_back(LocalSeconds(time));
			}
		}
		observances.push_back(observance);
	}
	if (!observances.empty())
		fZones[tzid] = observances;
}


bool
TimeZones::Knows(const std::string& tzid) const
{
	return fZones.find(tzid) != fZones.end();
}


bool
TimeZones::OffsetAt(const std::string& tzid, int64_t local, int* offset) const
{
	std::map<std::string, std::vector<Observance> >::const_iterator zone = fZones.find(tzid);
	if (zone == fZones.end()) {
		if (fallback)
			return fallback(tzid, local, offset);
		return false;
	}
	bool found = false;
	int64_t latest = 0;
	int offsetTo = 0;
	const Observance* earliest = NULL;
	for (const Observance& observance : zone->second) {
		const int64_t first = LocalSeconds(observance.onset);
		if (earliest == NULL || first < LocalSeconds(earliest->onset))
			earliest = &observance;
		int64_t last = 0;
		bool any = false;
		if (first <= local) {
			last = first;
			any = true;
		}
		for (int64_t extra : observance.extra) {
			if (extra <= local && (!any || extra > last)) {
				last = extra;
				any = true;
			}
		}
		if (observance.hasRule && first <= local) {
			// (a rule that begins in 1601 gives its instance of this year and the one before, not four centuries of them)
			DateTime start = observance.onset;
			if (observance.rule.frequency == Frequency::Yearly && observance.rule.interval == 1) {
				const int year = FromLocalSeconds(local).year - 1;
				if (year > start.year) {
					start.year = year;
					// (a rule that names its month or day does not use those of the start: the instances of the year begin at its first day)
					const Rule& r = observance.rule;
					if (!r.byMonth.empty() || !r.byMonthDay.empty() || !r.byDay.empty() || !r.byYearDay.empty() || !r.byWeekNo.empty()) {
						start.month = 1;
						start.day = 1;
						start.hour = start.minute = start.second = 0;
						// (the time of the change is that of the observance)
						start.hour = observance.onset.hour;
						start.minute = observance.onset.minute;
						start.second = observance.onset.second;
					}
				}
			}
			Expand(observance.rule, start, [&](const DateTime& time) {
				const int64_t seconds = LocalSeconds(time);
				if (seconds > local)
					return false;
				if (!any || seconds > last) {
					last = seconds;
					any = true;
				}
				return true;
			}, 1000);
		}
		if (any && (!found || last > latest)) {
			found = true;
			latest = last;
			offsetTo = observance.offsetTo;
		}
	}
	if (!found)
		offsetTo = earliest != NULL ? earliest->offsetFrom : 0;		// (before the first change)
	*offset = offsetTo;
	return true;
}


bool
PosixZoneOffset(const std::string& tzid, int64_t local, int* offset)
{
	// (only names like Europe/Vienna: a name that the library does not know would be taken as UTC without a word)
	if (tzid.find('/') == std::string::npos || tzid.find(' ') != std::string::npos || tzid.size() > 60)
		return false;
	const char* before = getenv("TZ");
	const std::string saved = before != NULL ? before : "";
	const bool had = before != NULL;
	setenv("TZ", tzid.c_str(), 1);
	tzset();
	const DateTime civil = FromLocalSeconds(local);
	struct tm parts;
	memset(&parts, 0, sizeof(parts));
	parts.tm_year = civil.year - 1900;
	parts.tm_mon = civil.month - 1;
	parts.tm_mday = civil.day;
	parts.tm_hour = civil.hour;
	parts.tm_min = civil.minute;
	parts.tm_sec = civil.second;
	parts.tm_isdst = -1;
	const time_t real = mktime(&parts);
	const bool ok = real != (time_t)-1;
	// (the name that the library found: for a zone that it does not know it is the name that we gave, not an abbreviation)
	const bool known = ok && parts.tm_zone != NULL && tzid.find(parts.tm_zone) == std::string::npos;
	if (had)
		setenv("TZ", saved.c_str(), 1);
	else
		unsetenv("TZ");
	tzset();
	if (!ok || !known)
		return false;
	*offset = (int)(local - (int64_t)real);
	return true;
}


bool
LocalZoneOffset(int64_t local, int* offset)
{
	const DateTime civil = FromLocalSeconds(local);
	struct tm parts;
	memset(&parts, 0, sizeof(parts));
	parts.tm_year = civil.year - 1900;
	parts.tm_mon = civil.month - 1;
	parts.tm_mday = civil.day;
	parts.tm_hour = civil.hour;
	parts.tm_min = civil.minute;
	parts.tm_sec = civil.second;
	parts.tm_isdst = -1;
	const time_t real = mktime(&parts);
	if (real == (time_t)-1)
		return false;
	*offset = (int)(local - (int64_t)real);
	return true;
}


// ---- the calendar

bool
Calendar::ToUnix(const DateTime& time, int64_t* seconds) const
{
	if (!time.valid)
		return false;
	const int64_t local = LocalSeconds(time);
	int offset = 0;
	if (time.utc) {
		*seconds = local;
		return true;
	}
	if (!time.tzid.empty() && zones.OffsetAt(time.tzid, local, &offset)) {
		*seconds = local - offset;
		return true;
	}
	// a floating time, a day, or a zone that is not known: the local time of this computer
	if (!LocalZoneOffset(local, &offset))
		offset = 0;
	*seconds = local - offset;
	return true;
}


const Event*
Calendar::MasterOf(const Event& event) const
{
	if (event.uid.empty())
		return event.hasRecurrenceId ? NULL : &event;
	for (const Event& other : events) {
		if (other.uid == event.uid && !other.hasRecurrenceId)
			return &other;
	}
	return NULL;
}


std::vector<const Event*>
Calendar::DistinctEvents() const
{
	std::vector<const Event*> distinct;
	for (const Event& event : events) {
		if (!event.uid.empty()) {
			bool seen = false;
			for (const Event* each : distinct)
				seen = seen || each->uid == event.uid;
			if (seen)
				continue;
			const Event* master = MasterOf(event);
			distinct.push_back(master != NULL ? master : &event);
		} else {
			distinct.push_back(&event);
		}
	}
	return distinct;
}


namespace {

Person
ReadPerson(const Property& property, bool vcalOld)
{
	Person person;
	std::string value = Trimmed(property.value);
	if (const std::string* name = property.ParamValue("CN"))
		person.name = Trimmed(*name);
	if (const std::string* role = property.ParamValue("ROLE"))
		person.role = Upper(*role);
	if (const std::string* status = property.ParamValue("PARTSTAT"))
		person.status = Upper(*status);
	else if (const std::string* old = property.ParamValue("STATUS"))
		person.status = Upper(*old);		// (vCalendar 1.0)
	if (const std::string* type = property.ParamValue("CUTYPE"))
		person.type = Upper(*type);

	// the value: `mailto:john@example.org`, `John Doe <john@example.org>` (vCalendar 1.0), a bare address or a bare name
	if (value.size() > 7 && Upper(value.substr(0, 7)) == "MAILTO:")
		value = Trimmed(value.substr(7));
	const size_t open = value.find('<');
	const size_t close = value.find('>');
	if (open != std::string::npos && close != std::string::npos && close > open) {
		if (person.name.empty())
			person.name = Trimmed(value.substr(0, open));
		person.email = Trimmed(value.substr(open + 1, close - open - 1));
		if (person.email.size() > 7 && Upper(person.email.substr(0, 7)) == "MAILTO:")
			person.email = person.email.substr(7);
	} else if (value.find('@') != std::string::npos && value.find(' ') == std::string::npos) {
		person.email = value;
	} else if (person.name.empty() && !value.empty() && (vcalOld || value.find(':') == std::string::npos)) {
		person.name = value;		// (a name, an address of another kind: `urn:`, `tel:`, is no name)
	}
	// (the name in quotes, as some programs write it)
	if (person.name.size() >= 2 && person.name.front() == '"' && person.name.back() == '"')
		person.name = person.name.substr(1, person.name.size() - 2);
	return person;
}

/** the times of a property: a list, each with the zone of the property; a period (`start/end`) is its start */
void
ReadTimes(const Property& property, std::vector<DateTime>* out)
{
	const std::string* tzid = property.ParamValue("TZID");
	const std::string* kind = property.ParamValue("VALUE");
	const bool dateOnly = kind != NULL && Upper(*kind) == "DATE";
	for (const std::string& each : SplitEscaped(property.value, ',')) {
		std::string text = Trimmed(each);
		const size_t slash = text.find('/');
		if (slash != std::string::npos)
			text = text.substr(0, slash);
		DateTime time;
		if (ParseDateTime(text, tzid != NULL ? Trimmed(*tzid) : std::string(), dateOnly, &time))
			out->push_back(time);
	}
}

std::string
DurationText(int64_t seconds)
{
	if (seconds <= 0)
		return seconds == 0 ? "PT0S" : "";
	std::string text = "P";
	const int64_t days = seconds / 86400;
	int64_t rest = seconds % 86400;
	if (days > 0)
		text += std::to_string(days) + "D";
	if (rest > 0) {
		text += "T";
		if (rest >= 3600) {
			text += std::to_string(rest / 3600) + "H";
			rest %= 3600;
		}
		if (rest >= 60) {
			text += std::to_string(rest / 60) + "M";
			rest %= 60;
		}
		if (rest > 0)
			text += std::to_string(rest) + "S";
	}
	return text;
}

Event
ReadEvent(const Component& component, int index, Calendar* calendar)
{
	Event event;
	event.index = index;
	auto text = [&](const char* name) {
		const Property* property = component.Find(name);
		return property != NULL ? Trimmed(UnescapeText(property->value)) : std::string();
	};
	event.uid = text("UID");
	event.summary = text("SUMMARY");
	event.description = text("DESCRIPTION");
	event.location = text("LOCATION");
	event.url = text("URL");
	event.status = Upper(text("STATUS"));
	event.transparency = Upper(text("TRANSP"));
	event.classification = Upper(text("CLASS"));
	if (const Property* sequence = component.Find("SEQUENCE"))
		event.sequence = atoi(sequence->value.c_str());
	for (const Property* property : component.FindAll("CATEGORIES")) {
		for (const std::string& each : SplitEscaped(property->value, ',')) {
			const std::string category = Trimmed(UnescapeText(each));
			if (!category.empty() && std::find(event.categories.begin(), event.categories.end(), category) == event.categories.end())
				event.categories.push_back(category);
		}
	}
	if (const Property* geo = component.Find("GEO")) {
		// `48.2082;16.3738` (a comma is taken as well)
		std::string value = Trimmed(geo->value);
		size_t separator = value.find(';');
		if (separator == std::string::npos)
			separator = value.find(',');
		if (separator != std::string::npos) {
			char* end = NULL;
			const double latitude = strtod(value.c_str(), &end);
			const double longitude = strtod(value.c_str() + separator + 1, NULL);
			if (end != value.c_str() && latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180 && !(latitude == 0 && longitude == 0)) {
				event.hasGeo = true;
				event.latitude = latitude;
				event.longitude = longitude;
			}
		}
	}
	const bool old = calendar->IsOldVersion();
	if (const Property* organizer = component.Find("ORGANIZER")) {
		event.organizer = ReadPerson(*organizer, old);
		event.organizer.role = "ORGANIZER";
		event.hasOrganizer = !event.organizer.name.empty() || !event.organizer.email.empty();
	}
	for (const Property* attendee : component.FindAll("ATTENDEE")) {
		Person person = ReadPerson(*attendee, old);
		if (!person.name.empty() || !person.email.empty())
			event.attendees.push_back(person);
	}

	// the times
	std::vector<DateTime> times;
	if (const Property* start = component.Find("DTSTART")) {
		ReadTimes(*start, &times);
		if (!times.empty()) {
			event.start = times[0];
			event.hasStart = true;
			event.allDay = event.start.dateOnly;
		} else {
			calendar->notes.push_back("DTSTART could not be read: " + start->value.substr(0, 30));
		}
	}
	times.clear();
	if (const Property* end = component.Find("DTEND")) {
		ReadTimes(*end, &times);
		if (!times.empty()) {
			event.end = times[0];
			event.hasEnd = true;
		}
	} else if (const Property* due = component.Find("DUE")) {
		ReadTimes(*due, &times);
		if (!times.empty()) {
			event.end = times[0];
			event.hasEnd = true;
		}
	}
	if (const Property* duration = component.Find("DURATION")) {
		int64_t seconds = 0;
		if (ParseDuration(duration->value, &seconds) && seconds >= 0) {
			event.hasDuration = true;
			event.durationSeconds = seconds;
			event.durationText = Upper(Trimmed(duration->value));
		}
	}

	// the end: given, from the duration, a day for a day, else the event is an instant
	if (event.hasStart && !event.hasEnd) {
		if (event.hasDuration) {
			int64_t startSeconds = 0;
			if (calendar->ToUnix(event.start, &startSeconds)) {
				// (the end is in the same zone as the start: add to the civil time, so that a day is a day across a change of the clock)
				event.end = FromLocalSeconds(LocalSeconds(event.start) + event.durationSeconds);
				event.end.utc = event.start.utc;
				event.end.tzid = event.start.tzid;
				event.end.dateOnly = event.start.dateOnly && event.durationSeconds % 86400 == 0;
				event.hasEnd = true;
			}
		} else if (event.allDay) {
			event.end = FromLocalSeconds(LocalSeconds(event.start) + 86400);
			event.end.dateOnly = true;
			event.hasEnd = true;
		}
	}
	if (event.hasStart) {
		event.startKnown = calendar->ToUnix(event.start, &event.startUnix);
		if (event.hasEnd)
			event.endKnown = calendar->ToUnix(event.end, &event.endUnix);
		if (event.endKnown && event.endUnix < event.startUnix) {
			calendar->notes.push_back("the end is before the start: " + event.summary);
			event.endKnown = false;
			event.hasEnd = false;
		}
		if (event.startKnown && event.endKnown && !event.hasDuration) {
			event.hasDuration = true;
			event.durationSeconds = event.endUnix - event.startUnix;
			event.durationText = DurationText(event.durationSeconds);
		}
	}

	// the recurrence
	if (const Property* rule = component.Find("RRULE")) {
		event.ruleText = Trimmed(rule->value);
		if (ParseRule(rule->value, &event.rule))
			event.hasRule = true;
		else
			calendar->notes.push_back("a recurrence rule that could not be read: " + event.ruleText.substr(0, 60));
	}
	for (const Property* property : component.FindAll("EXDATE"))
		ReadTimes(*property, &event.exceptions);
	for (const Property* property : component.FindAll("RDATE"))
		ReadTimes(*property, &event.extraDates);
	if (const Property* id = component.Find("RECURRENCE-ID")) {
		times.clear();
		ReadTimes(*id, &times);
		if (!times.empty()) {
			event.recurrenceId = times[0];
			event.hasRecurrenceId = true;
		}
	}
	return event;
}

void
CollectEvents(const Component& component, Calendar* calendar)
{
	for (const Component& child : component.children) {
		if (child.name == "VEVENT")
			calendar->events.push_back(ReadEvent(child, (int)calendar->events.size(), calendar));
		else if (child.name == "VTIMEZONE")
			calendar->zones.Add(child);
		else
			CollectEvents(child, calendar);
	}
}

}	// namespace


Calendar
ReadCalendar(const std::string& text)
{
	Calendar calendar;
	calendar.zones.fallback = PosixZoneOffset;
	Parsed parsed = Parse(text);
	calendar.notes = parsed.notes;

	// the properties of the calendar, and its zones first (the events need them)
	for (const Component& root : parsed.components) {
		if (root.name != "VCALENDAR")
			continue;
		auto value = [&](const char* name) {
			const Property* property = root.Find(name);
			return property != NULL ? Trimmed(UnescapeText(property->value)) : std::string();
		};
		if (calendar.version.empty())
			calendar.version = value("VERSION");
		if (calendar.method.empty())
			calendar.method = Upper(value("METHOD"));
		if (calendar.productId.empty())
			calendar.productId = value("PRODID");
		if (calendar.name.empty())
			calendar.name = value("X-WR-CALNAME").empty() ? value("NAME") : value("X-WR-CALNAME");
		if (calendar.timezone.empty())
			calendar.timezone = value("X-WR-TIMEZONE");
	}
	std::vector<const Component*> pending;
	std::function<void(const Component&)> zonesOf = [&](const Component& component) {
		for (const Component& child : component.children) {
			if (child.name == "VTIMEZONE")
				calendar.zones.Add(child);
			else
				zonesOf(child);
		}
	};
	for (const Component& root : parsed.components) {
		if (root.name == "VTIMEZONE")
			calendar.zones.Add(root);
		else
			zonesOf(root);
	}
	for (const Component& root : parsed.components) {
		if (root.name == "VEVENT")
			calendar.events.push_back(ReadEvent(root, (int)calendar.events.size(), &calendar));
		else
			CollectEvents(root, &calendar);
	}
	return calendar;
}


std::string
OccurrenceKey(const DateTime& slot)
{
	char buffer[40];
	if (slot.dateOnly)
		snprintf(buffer, sizeof(buffer), "%04d%02d%02d", slot.year, slot.month, slot.day);
	else
		snprintf(buffer, sizeof(buffer), "%04d%02d%02dT%02d%02d%02d%s", slot.year, slot.month, slot.day, slot.hour, slot.minute, slot.second, slot.utc ? "Z" : "");
	return buffer;
}


bool
SeriesNeighbours(const Calendar& calendar, const Event& event, bool* hasPrevious, Occurrence* previous, bool* hasNext, Occurrence* next)
{
	*hasPrevious = *hasNext = false;
	const Event* master = calendar.MasterOf(event);
	if (master == NULL || !master->hasRule || !master->hasStart)
		return false;
	DateTime own = event.hasRecurrenceId ? event.recurrenceId : master->start;
	if (!own.valid)
		return false;
	const int64_t ownLocal = LocalSeconds(own);

	// an UNTIL in UTC is a moment of the zone of the start: compared as the civil time there
	Rule rule = master->rule;
	if (rule.until.valid && rule.until.utc && !master->start.utc) {
		int offset = 0;
		const int64_t untilUtc = LocalSeconds(rule.until);
		const std::string& tzid = master->start.tzid;
		bool known = !tzid.empty() && calendar.zones.OffsetAt(tzid, untilUtc, &offset);
		if (!known)
			known = LocalZoneOffset(untilUtc, &offset);
		if (known) {
			rule.until = FromLocalSeconds(untilUtc + offset);
			rule.until.dateOnly = false;
		}
	}

	std::vector<int64_t> excluded;
	for (const DateTime& each : master->exceptions)
		excluded.push_back(LocalSeconds(each));
	auto isExcluded = [&](int64_t local) { return std::find(excluded.begin(), excluded.end(), local) != excluded.end(); };

	bool havePrev = false, haveNext = false;
	int64_t prevLocal = 0, nextLocal = 0;
	Expand(rule, master->start, [&](const DateTime& time) {
		const int64_t local = LocalSeconds(time);
		if (isExcluded(local))
			return true;
		if (local < ownLocal) {
			havePrev = true;
			prevLocal = local;
			return true;
		}
		if (local > ownLocal) {
			haveNext = true;
			nextLocal = local;
			return false;
		}
		return true;
	}, 200000);
	// (the dates that are added to the series)
	for (const DateTime& each : master->extraDates) {
		const int64_t local = LocalSeconds(each);
		if (isExcluded(local))
			continue;
		if (local < ownLocal && (!havePrev || local > prevLocal)) {
			havePrev = true;
			prevLocal = local;
		} else if (local > ownLocal && (!haveNext || local < nextLocal)) {
			haveNext = true;
			nextLocal = local;
		}
	}

	auto make = [&](int64_t local, Occurrence* occurrence) {
		DateTime slot = FromLocalSeconds(local);
		slot.dateOnly = master->start.dateOnly;
		slot.utc = master->start.utc;
		slot.tzid = master->start.tzid;
		occurrence->start = slot;
		occurrence->summary = master->summary;
		// the length of the series' own event, from the master
		int64_t length = master->startKnown && master->endKnown ? master->endUnix - master->startUnix : 0;
		// an instance that the file moved or changed has its own times
		for (const Event& other : calendar.events) {
			if (&other != master && other.uid == master->uid && other.hasRecurrenceId && LocalSeconds(other.recurrenceId) == local && other.startKnown) {
				occurrence->startUnix = other.startUnix;
				occurrence->endUnix = other.endKnown ? other.endUnix : other.startUnix;
				if (!other.summary.empty())
					occurrence->summary = other.summary;
				occurrence->moved = true;
				return true;
			}
		}
		int64_t startUnix = 0;
		if (!calendar.ToUnix(slot, &startUnix))
			return false;
		occurrence->startUnix = startUnix;
		occurrence->endUnix = startUnix + length;
		return true;
	};
	if (havePrev)
		*hasPrevious = make(prevLocal, previous);
	if (haveNext)
		*hasNext = make(nextLocal, next);
	return true;
}

}	// namespace ics
