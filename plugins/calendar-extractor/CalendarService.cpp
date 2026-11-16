/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "CalendarService.h"

#include <Directory.h>
#include <Messenger.h>
#include <Node.h>
#include <String.h>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <iostream>
#include <vector>

#include <sen/Sen.h>
#include <sen/SenAttributes.h>
#include <sen/SenMessages.h>
#include <sen/SenOntoCore.h>
#include <sen/SenOntoPeople.h>
#include <sen/SenOntoSpatial.h>
#include <sen/SenOntoTemporal.h>
#include <sen/Sensei.h>

#include "../../common/EntityFile.h"
#include "CalendarStore.h"
#include "core/CalendarFacts.h"
#include "core/CalendarSettings.h"
#include "core/IcsText.h"

namespace temporal = sen::onto::temporal;
namespace people = sen::onto::people;
namespace spatial = sen::onto::spatial;

namespace {

const size_t kFileLimit = 8 * 1024 * 1024;		// (a calendar file of eight megabytes is an export of years: more is not read)
const char* const kSignature = "application/x-vnd.sen-plugin.sen-labs.calendar-extractor";

bool
ReadCalendarOf(const entry_ref& file, ics::Calendar* calendar)
{
	std::string text;
	if (!calendar::store::ReadText(file, kFileLimit, &text) || !ics::LooksLikeCalendar(text.substr(0, 4096)))
		return false;
	*calendar = ics::ReadCalendar(text);
	return !calendar->events.empty();
}

std::string
Lower(const std::string& text)
{
	std::string out = text;
	for (char& c : out)
		c = (char)tolower((unsigned char)c);
	return out;
}

/** the first line of the text of a place, cut to a name */
std::string
PlaceName(const std::string& location)
{
	std::string name = location.substr(0, location.find('\n'));
	while (!name.empty() && isspace((unsigned char)name.back()))
		name.pop_back();
	if (name.size() > 100) {
		size_t cut = 100;
		while (cut > 0 && ((unsigned char)name[cut] & 0xC0) == 0x80)
			cut--;
		name = name.substr(0, cut);
	}
	return name;
}

}	// namespace


status_t
CalendarService::Identify(const entry_ref& file, BMessage* reply)
{
	ics::Calendar calendar;
	if (!ReadCalendarOf(file, &calendar)) {
		fprintf(stderr, "%s is no calendar file\n", file.name);
		return B_ENTRY_NOT_FOUND;
	}
	// one event is an event, several are a calendar that holds them
	const bool single = calendar.DistinctEvents().size() == 1;
	const char* type = single ? temporal::mime::kEvent : temporal::mime::kCalendar;
	status_t status = calendar::store::SetTypes(file, type);
	if (status != B_OK)
		return status;
	reply->AddString(sensei::key::kSemanticType, type);
	fprintf(stderr, "%s is %s\n", file.name, single ? "an event" : "a calendar");
	return B_OK;
}


status_t
CalendarService::Enrich(entry_ref* file, BMessage* reply)
{
	fSummaryTitle.clear();
	fSummary.clear();
	ics::Calendar calendar;
	if (!ReadCalendarOf(*file, &calendar)) {
		fSummaryTitle = "Calendar not enriched";
		fSummary = std::string(file->name) + " has no event that could be read.";
		return B_ENTRY_NOT_FOUND;
	}
	const std::vector<const ics::Event*> events = calendar.DistinctEvents();

	if (events.size() > 1) {
		// a calendar of several events: the file is no event; it has its name and the time that its events cover
		int64_t first = 0, last = 0;
		bool any = false;
		for (const ics::Event* event : events) {
			if (!event->startKnown)
				continue;
			const int64_t end = event->endKnown ? event->endUnix : event->startUnix;
			first = any ? std::min(first, event->startUnix) : event->startUnix;
			last = any ? std::max(last, end) : end;
			any = true;
		}
		calendar::store::SetTypes(*file, temporal::mime::kCalendar);
		reply->AddString(sensei::key::kSemanticType, temporal::mime::kCalendar);
		BNode node(file);
		std::string name = calendar.name;
		if (name.empty()) {
			// (a calendar that has no name is called after its file)
			name = file->name;
			const size_t dot = name.rfind('.');
			if (dot != std::string::npos && dot > 0)
				name = name.substr(0, dot);
		}
		calendar::store::WriteCalendar(node, name, calendar.description, (int32)events.size(), first, last, any);
		fSummaryTitle = "Calendar enriched";
		fSummary = std::string(file->name) + " has " + std::to_string(events.size()) + " events" + (calendar.name.empty() ? "" : " (" + calendar.name + ")") + ".";
		return B_OK;
	}

	const ics::Event& event = *events[0];
	const calendar::Facts facts = calendar::FactsOf(calendar, event, fOptions.namePattern);
	status_t status = calendar::store::SetTypes(*file, temporal::mime::kEvent);
	if (status != B_OK)
		return status;
	{
		BNode node(file);
		if ((status = node.InitCheck()) != B_OK)
			return status;
		calendar::store::WriteEvent(node, facts);
	}
	reply->AddString(sensei::key::kSemanticType, temporal::mime::kEvent);

	// the name of the file and its description: both carry the date and time of the event
	std::string note;
	if (fOptions.rename && !facts.baseName.empty()) {
		const status_t renamed = calendar::store::Rename(file, facts.baseName);
		if (renamed != B_OK)
			note += " The file could not be renamed: " + std::string(strerror(renamed)) + ".";
	}
	if (fOptions.description && !facts.markdown.empty()) {
		const std::string base = facts.baseName.empty() ? std::string() : facts.baseName;
		std::string fileBase = file->name;
		const size_t dot = fileBase.rfind('.');
		if (dot != std::string::npos && dot > 0)
			fileBase = fileBase.substr(0, dot);
		entry_ref description;
		const status_t written = calendar::store::WriteDescriptionFile(*file, fOptions.rename && !base.empty() ? base : fileBase, facts.title, facts.markdown, &description);
		if (written == B_OK)
			note += " The description is in " + std::string(description.name) + ".";
		else if (written == B_FILE_EXISTS)
			note += " A file of that name is there already: the description was not written.";
	}
	fSummaryTitle = "Event enriched";
	fSummary = (facts.title.empty() ? std::string(file->name) : facts.title) + (facts.hasStart ? " (" + ics::DateTimeText(facts.localStart) + ")" : "") + "." + note;
	return B_OK;
}


status_t
CalendarService::Resolve(const entry_ref& file, const char* type, const BMessage& libraries, BMessage* reply)
{
	ics::Calendar calendar;
	if (!ReadCalendarOf(file, &calendar))
		return B_ENTRY_NOT_FOUND;
	const std::vector<const ics::Event*> events = calendar.DistinctEvents();
	const std::string kind = type != NULL ? type : "";

	if (kind == temporal::mime::kCalendarEvents) {
		// the events of a calendar are places in the same file, as the pages of a document are: the target is the file, an event is named by its UID
		for (const ics::Event* each : events) {
			BMessage item;
			item.AddRef(sen::key::kTargetRef, &file);
			item.AddString(sen::attr::kRelationLabel, "contains");
			item.AddString(temporal::attr::kTitle, each->summary.c_str());
			if (each->startKnown)
				item.AddInt64(temporal::attr::kStartDate, each->startUnix);
			if (each->endKnown)
				item.AddInt64(temporal::attr::kEndDate, each->endUnix);
			if (!each->uid.empty())
				item.AddString(temporal::attr::kUid, each->uid.c_str());
			item.AddString(sen::onto::core::attr::kRelProvenance, kSignature);
			reply->AddMessage(sensei::key::kTargets, &item);
		}
		return B_OK;
	}

	if (events.size() != 1)
		return B_ENTRY_NOT_FOUND;
	const ics::Event& event = *events[0];

	auto libraryOf = [&](const char* mimeType, entry_ref* folder) {
		return libraries.FindRef(mimeType, folder) == B_OK;
	};

	if (kind == temporal::mime::kEventAttendee) {
		std::vector<ics::Person> all;
		if (event.hasOrganizer)
			all.push_back(event.organizer);
		for (const ics::Person& person : event.attendees)
			all.push_back(person);
		std::vector<std::string> seen;
		entry_ref library;
		const bool haveLibrary = libraryOf(people::mime::kPerson, &library);
		for (const ics::Person& person : all) {
			const std::string name = calendar::NaturalName(person.name.empty() ? person.email.substr(0, person.email.find('@')) : person.name);
			const std::string email = Lower(person.email);
			const std::string key = email.empty() ? Lower(name) : email;
			if (name.empty() || std::find(seen.begin(), seen.end(), key) != seen.end())
				continue;
			seen.push_back(key);
			entry_ref ref;
			bool created = false;
			const status_t status = sensei::FindOrCreateEntityFile(file, people::mime::kPerson, name.c_str(), email.c_str(), [&](BNode& node) {
				BString mbox;
				// (the same name with another address is another person)
				return email.empty() || node.ReadAttrString(people::attr::kMbox, &mbox) != B_OK || mbox.IsEmpty() || Lower(mbox.String()) == email;
			}, &ref, &created, haveLibrary ? &library : NULL);
			if (status != B_OK) {
				fprintf(stderr, "no file for %s: %s\n", name.c_str(), strerror(status));
				continue;
			}
			BNode node(&ref);
			BString current;
			if (node.ReadAttrString(people::attr::kName, &current) != B_OK || current.IsEmpty())
				node.WriteAttr(people::attr::kName, B_STRING_TYPE, 0, name.c_str(), name.size() + 1);
			if (!email.empty() && (node.ReadAttrString(people::attr::kMbox, &current) != B_OK || current.IsEmpty()))
				node.WriteAttr(people::attr::kMbox, B_STRING_TYPE, 0, email.c_str(), email.size() + 1);
			BMessage item;
			item.AddRef(sen::key::kTargetRef, &ref);
			item.AddString(sen::attr::kRelationLabel, "attendee");
			item.AddString(sen::onto::core::attr::kRelRole, calendar::RoleText(person).c_str());
			const std::string response = calendar::ResponseText(person.status);
			if (!response.empty())
				item.AddString(temporal::attr::kResponse, response.c_str());
			// (the user is told among the others: the Kind of the relation is "you")
			calendar::Settings who;
			who.myself = fOptions.myself;
			if (calendar::IsMyself(who, person.name, person.email))
				item.AddString(sen::attr::kRelationKind, "you");
			item.AddString(sen::onto::core::attr::kRelProvenance, kSignature);
			reply->AddMessage(sensei::key::kTargets, &item);
		}
		return B_OK;
	}

	if (kind == temporal::mime::kEventDescription) {
		// the Markdown file beside the event, with the name of the event: the one that this plugin made
		std::string name = file.name;
		const size_t dot = name.rfind('.');
		if (dot != std::string::npos && dot > 0)
			name = name.substr(0, dot);
		BEntry parentEntry(&file);
		BDirectory parent;
		if (parentEntry.InitCheck() != B_OK || parentEntry.GetParent(&parent) != B_OK)
			return B_OK;
		BEntry description(&parent, (name + ".md").c_str());
		entry_ref ref;
		if (!description.Exists() || description.GetRef(&ref) != B_OK)
			return B_OK;
		BMessage item;
		item.AddRef(sen::key::kTargetRef, &ref);
		item.AddString(sen::attr::kRelationLabel, "described in");
		item.AddString(sen::onto::core::attr::kRelProvenance, kSignature);
		reply->AddMessage(sensei::key::kTargets, &item);
		return B_OK;
	}

	if (kind == temporal::mime::kEventLocation) {
		const std::string name = PlaceName(event.location);
		// (a location that is a link is the address of the meeting, not a place)
		if (name.empty() || !ics::FindUrls(name).empty())
			return B_OK;
		entry_ref library;
		const bool haveLibrary = libraryOf(spatial::mime::kPlace, &library);
		entry_ref ref;
		bool created = false;
		const status_t status = sensei::FindOrCreateEntityFile(file, spatial::mime::kPlace, name.c_str(), event.uid.c_str(), nullptr, &ref, &created, haveLibrary ? &library : NULL);
		if (status != B_OK) {
			fprintf(stderr, "no file for %s: %s\n", name.c_str(), strerror(status));
			return status;
		}
		if (created) {
			BNode node(&ref);
			node.WriteAttr(spatial::attr::kTitle, B_STRING_TYPE, 0, name.c_str(), name.size() + 1);
			if (event.location.size() > name.size())
				node.WriteAttr(spatial::attr::kAddress, B_STRING_TYPE, 0, event.location.c_str(), event.location.size() + 1);
			if (event.hasGeo) {
				node.WriteAttr(spatial::attr::kLatitude, B_DOUBLE_TYPE, 0, &event.latitude, sizeof(double));
				node.WriteAttr(spatial::attr::kLongitude, B_DOUBLE_TYPE, 0, &event.longitude, sizeof(double));
			}
			// a place that is new has no outline or picture: Spaces enriches it in the background
			BMessenger server(sen::kServerSignature);
			if (server.IsValid()) {
				BMessage enrich(sen::cmd::kEnrichRefs);
				enrich.AddRef("refs", &ref);
				enrich.AddBool("wipe", false);
				enrich.AddBool("auto", true);
				server.SendMessage(&enrich);
			}
		}
		BMessage item;
		item.AddRef(sen::key::kTargetRef, &ref);
		item.AddString(sen::attr::kRelationLabel, "takes place at");
		item.AddString(sen::onto::core::attr::kRelProvenance, kSignature);
		reply->AddMessage(sensei::key::kTargets, &item);
		return B_OK;
	}

	if (kind == temporal::mime::kEventSeries) {
		bool hasPrevious = false, hasNext = false;
		ics::Occurrence previous, next;
		if (!ics::SeriesNeighbours(calendar, event, &hasPrevious, &previous, &hasNext, &next))
			return B_OK;		// (no series: nothing to show)
		auto add = [&](const ics::Occurrence& occurrence, const char* label) {
			// an instance is a place in the same file, as the page of a document is: the target is the file, the instance is named by its RECURRENCE-ID
			BMessage item;
			item.AddRef(sen::key::kTargetRef, &file);
			item.AddString(sen::attr::kRelationLabel, label);
			item.AddString(temporal::attr::kTitle, occurrence.summary.c_str());
			item.AddInt64(temporal::attr::kStartDate, occurrence.startUnix);
			item.AddInt64(temporal::attr::kEndDate, occurrence.endUnix);
			item.AddString(temporal::attr::kRecurrenceId, ics::OccurrenceKey(occurrence.start).c_str());
			item.AddString(sen::onto::core::attr::kRelProvenance, kSignature);
			reply->AddMessage(sensei::key::kTargets, &item);
		};
		if (hasPrevious)
			add(previous, "previous");
		if (hasNext)
			add(next, "next");
		return B_OK;
	}
	return B_BAD_VALUE;
}


status_t
CalendarService::Describe(const entry_ref& file)
{
	ics::Calendar calendar;
	if (!ReadCalendarOf(file, &calendar)) {
		std::cout << file.name << ": no calendar file, or no event in it\n";
		return B_ENTRY_NOT_FOUND;
	}
	for (const std::string& note : calendar.notes)
		std::cout << "note: " << note << "\n";
	for (const ics::Event* event : calendar.DistinctEvents()) {
		const calendar::Facts f = calendar::FactsOf(calendar, *event, fOptions.namePattern);
		std::cout << "--- " << (f.title.empty() ? "(no title)" : f.title) << "\n";
		std::cout << "name:        " << f.baseName << "\n";
		std::cout << "start:       " << (f.hasStart ? std::to_string(f.startUnix) + " (UTC), local " + ics::DateTimeText(f.localStart) : "-") << (f.startEdtf.empty() ? "" : "  EDTF " + f.startEdtf) << "\n";
		std::cout << "end:         " << (f.hasEnd ? std::to_string(f.endUnix) : "-") << (f.endEdtf.empty() ? "" : "  EDTF " + f.endEdtf) << "\n";
		std::cout << "status:      " << f.status << "   attendance: " << f.attendanceMode << "   uid: " << f.uid << "\n";
		std::cout << "location:    " << f.location << "\n";
		std::cout << "organizer:   " << f.organizer << "\n";
		std::cout << "meeting:     " << f.meetingType << " " << f.meetingUrl << "\n";
		std::cout << "recurrence:  " << f.recurrence << "\n";
		std::cout << "summary:     " << f.summary << "\n";
		for (const ics::Person& person : event->attendees)
			std::cout << "attendee:    " << calendar::PersonText(person) << " (" << calendar::RoleText(person) << ", " << calendar::ResponseText(person.status) << ")\n";
		for (const std::string& link : f.links)
			std::cout << "link:        " << link << "\n";
		if (!f.markdown.empty())
			std::cout << "markdown:\n" << f.markdown;
		bool hasPrevious, hasNext;
		ics::Occurrence previous, next;
		if (ics::SeriesNeighbours(calendar, *event, &hasPrevious, &previous, &hasNext, &next)) {
			if (hasPrevious)
				std::cout << "previous:    " << ics::OccurrenceKey(previous.start) << " " << previous.summary << "\n";
			if (hasNext)
				std::cout << "next:        " << ics::OccurrenceKey(next.start) << " " << next.summary << "\n";
		}
	}
	return B_OK;
}
