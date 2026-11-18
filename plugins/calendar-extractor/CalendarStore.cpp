/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "CalendarStore.h"

#include <Directory.h>
#include <File.h>
#include <Query.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <NodeInfo.h>
#include <Path.h>
#include <String.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <sen/Sen.h>
#include <sen/SenAttributes.h>
#include <sen/SenOntoTemporal.h>

namespace calendar {
namespace store {

namespace temporal = sen::onto::temporal;

namespace {

const char* const kMarker = "<!-- SEN calendar extractor: made from the calendar file, written again when it is enriched -->";

/** a string attribute; an empty text takes the attribute away */
void
PutString(BNode& node, const char* name, const std::string& value)
{
	if (value.empty()) {
		node.RemoveAttr(name);
		return;
	}
	node.WriteAttr(name, B_STRING_TYPE, 0, value.c_str(), value.size() + 1);
}

void
PutTime(BNode& node, const char* name, bool present, int64_t value)
{
	if (!present) {
		node.RemoveAttr(name);
		return;
	}
	const time_t t = (time_t)value;
	node.WriteAttr(name, B_TIME_TYPE, 0, &t, sizeof(t));
}

}	// namespace


bool
ReadText(const entry_ref& file, size_t limit, std::string* text)
{
	BFile in(&file, B_READ_ONLY);
	off_t size = 0;
	if (in.InitCheck() != B_OK || in.GetSize(&size) != B_OK || size <= 0 || (size_t)size > limit)
		return false;
	text->assign((size_t)size, '\0');
	return in.Read(&(*text)[0], (size_t)size) == size;
}


status_t
SetTypes(const entry_ref& file, const char* semanticType)
{
	BNode node(&file);
	status_t status = node.InitCheck();
	if (status != B_OK)
		return status;
	// (the Haiku type first: a file with META:TYPE and no BEOS:TYPE is taken for an entry of the MIME database)
	BNodeInfo info(&node);
	char haikuType[B_MIME_TYPE_LENGTH];
	if (info.GetType(haikuType) != B_OK || haikuType[0] == '\0')
		info.SetType("text/calendar");
	if (semanticType == NULL)
		return B_OK;
	BString current;
	if (node.ReadAttrString(sen::attr::kType, &current) == B_OK && current == semanticType)
		return B_OK;
	const ssize_t written = node.WriteAttr(sen::attr::kType, B_MIME_STRING_TYPE, 0, semanticType, strlen(semanticType) + 1);
	return written == (ssize_t)strlen(semanticType) + 1 ? B_OK : (written < 0 ? (status_t)written : B_IO_ERROR);
}


status_t
WriteEvent(BNode& node, const Facts& facts)
{
	PutString(node, temporal::attr::kTitle, facts.title);
	PutString(node, temporal::attr::kDescription, facts.summary);
	PutString(node, temporal::attr::kUid, facts.uid);
	PutString(node, temporal::attr::kLocation, facts.location);
	PutString(node, temporal::attr::kOrganizer, facts.organizer);
	PutString(node, temporal::attr::kEventStatus, facts.status);
	PutString(node, temporal::attr::kAttendanceMode, facts.attendanceMode);
	PutString(node, temporal::attr::kUrl, facts.url);
	PutString(node, temporal::attr::kMeetingUrl, facts.meetingUrl);
	PutString(node, temporal::attr::kMeetingType, facts.meetingType);
	PutString(node, temporal::attr::kRecurrence, facts.recurrence);
	PutString(node, temporal::attr::kRepeats, facts.repeats);
	{
		// (a list attribute is one text with the separator of the attribute between the entries)
		std::string people;
		for (const std::string& person : facts.attendees) {
			if (!people.empty())
				people += ";";
			people += person;
		}
		PutString(node, temporal::attr::kAttendees, people);
	}
	PutTime(node, temporal::attr::kStartDate, facts.hasStart, facts.startUnix);
	PutTime(node, temporal::attr::kEndDate, facts.hasEnd, facts.endUnix);
	PutString(node, temporal::attr::kStartDateEdtf, facts.startEdtf);
	PutString(node, temporal::attr::kEndDateEdtf, facts.endEdtf);
	PutString(node, temporal::attr::kDuration, facts.hasEnd ? std::string() : facts.duration);
	return B_OK;
}


status_t
WriteCalendar(BNode& node, const std::string& name, const std::string& description, int32 events, int64_t first, int64_t last, bool hasTimes)
{
	PutString(node, temporal::attr::kTitle, name);
	PutString(node, temporal::attr::kDescription, description);
	node.WriteAttr(temporal::attr::kEventCount, B_INT32_TYPE, 0, &events, sizeof(events));
	PutTime(node, temporal::attr::kStartDate, hasTimes, first);
	PutTime(node, temporal::attr::kEndDate, hasTimes, last);
	return B_OK;
}


status_t
Rename(entry_ref* file, const std::string& baseName)
{
	if (baseName.empty())
		return B_BAD_VALUE;
	std::string extension = ".ics";
	const char* dot = strrchr(file->name, '.');
	if (dot != NULL && dot != file->name && strlen(dot) <= 5)
		extension = dot;

	BEntry entry(file);
	BEntry parentEntry;
	BDirectory parent;
	status_t status = entry.InitCheck();
	if (status == B_OK)
		status = entry.GetParent(&parent);
	if (status != B_OK)
		return status;

	for (int attempt = 1; attempt < 100; attempt++) {
		std::string candidate = baseName + (attempt == 1 ? std::string() : "-" + std::to_string(attempt)) + extension;
		if (candidate == file->name)
			return B_OK;
		BEntry other(&parent, candidate.c_str());
		if (other.Exists()) {
			if (other == entry)
				return B_OK;
			continue;
		}
		status = entry.Rename(candidate.c_str(), false);
		if (status == B_OK)
			return entry.GetRef(file);
		if (status != B_FILE_EXISTS)
			return status;
	}
	return B_NAME_IN_USE;
}


status_t
WriteDescriptionFile(const entry_ref& beside, const std::string& baseName, const std::string& title, const std::string& markdown, entry_ref* created)
{
	if (markdown.empty())
		return B_OK;
	BEntry besideEntry(&beside);
	BDirectory parent;
	status_t status = besideEntry.InitCheck();
	if (status == B_OK)
		status = besideEntry.GetParent(&parent);
	if (status != B_OK)
		return status;

	const std::string name = baseName + ".md";
	BEntry existing(&parent, name.c_str());
	if (existing.Exists()) {
		// (a file that somebody else made, or has changed, is theirs)
		entry_ref ref;
		std::string text;
		if (existing.GetRef(&ref) != B_OK || !ReadText(ref, 1024 * 1024, &text) || text.find(kMarker) == std::string::npos)
			return B_FILE_EXISTS;
	}
	BFile file;
	status = parent.CreateFile(name.c_str(), &file, false);
	if (status == B_FILE_EXISTS) {
		file.SetTo(&parent, name.c_str(), B_WRITE_ONLY | B_ERASE_FILE);
		status = file.InitCheck();
	}
	if (status != B_OK)
		return status;
	std::string text = kMarker;
	text += "\n";
	if (!title.empty())
		text += "# " + title + "\n\n";
	text += markdown;
	if (file.Write(text.data(), text.size()) != (ssize_t)text.size())
		return B_IO_ERROR;
	BNodeInfo info(&file);
	info.SetType("text/markdown");
	if (!title.empty())
		PutString(file, temporal::attr::kTitle, title);
	BEntry made(&parent, name.c_str());
	return created != NULL ? made.GetRef(created) : B_OK;
}

namespace {

bool
IsKeptEvent(const entry_ref& ref, const std::string& key)
{
	BPath path(&ref);
	std::string text = path.Path() != NULL ? path.Path() : "";
	for (char& c : text)
		c = (char)tolower((unsigned char)c);
	if (text.find("/trash/") != std::string::npos)
		return false;
	BNode node(&ref);
	if (node.InitCheck() != B_OK)
		return false;
	BString id;
	if (node.ReadAttrString(temporal::attr::kUid, &id) != B_OK || key != id.String())
		return false;
	// (a calendar file, or what a plugin has typed as an event)
	char type[B_MIME_TYPE_LENGTH];
	type[0] = '\0';
	BNodeInfo(&node).GetType(type);
	BString semantic;
	node.ReadAttrString(sen::attr::kType, &semantic);
	return strncmp(type, "text/", 5) == 0 || semantic == temporal::mime::kEvent;
}

}	// namespace


bool
FindEventFile(const std::string& key, const entry_ref* folder, entry_ref* found, bool onlyInFolder)
{
	if (key.empty())
		return false;
	BVolumeRoster roster;
	BVolume volume;
	while (!onlyInFolder && roster.GetNextVolume(&volume) == B_OK) {
		if (!volume.KnowsQuery())
			continue;
		BQuery query;
		query.SetVolume(&volume);
		query.PushAttr(temporal::attr::kUid);
		query.PushString(key.c_str());
		query.PushOp(B_EQ);
		if (query.Fetch() != B_OK)
			continue;
		entry_ref ref;
		while (query.GetNextRef(&ref) == B_OK) {
			if (IsKeptEvent(ref, key)) {
				*found = ref;
				return true;
			}
		}
	}
	// no index (a volume that was not set up for the attributes of SEN): the files of the folder
	if (folder != NULL) {
		BDirectory directory(folder);
		entry_ref ref;
		while (directory.InitCheck() == B_OK && directory.GetNextRef(&ref) == B_OK) {
			if (IsKeptEvent(ref, key)) {
				*found = ref;
				return true;
			}
		}
	}
	return false;
}


status_t
CreateCalendarFile(const entry_ref& folder, const std::string& title, const std::string& text, entry_ref* created)
{
	BDirectory directory(&folder);
	status_t status = directory.InitCheck();
	if (status != B_OK)
		return status;
	std::string base = title.empty() ? "Event" : title;
	for (char& c : base) {
		if (c == '/')
			c = '-';
	}
	if (base.size() > 80) {
		size_t cut = 80;
		while (cut > 0 && ((unsigned char)base[cut] & 0xC0) == 0x80)
			cut--;
		base.resize(cut);
	}
	for (int attempt = 1; attempt < 100; attempt++) {
		const std::string name = base + (attempt == 1 ? std::string() : "-" + std::to_string(attempt)) + ".ics";
		BFile file;
		status = directory.CreateFile(name.c_str(), &file, true);
		if (status == B_FILE_EXISTS)
			continue;
		if (status != B_OK)
			return status;
		if (file.Write(text.data(), text.size()) != (ssize_t)text.size())
			return B_IO_ERROR;
		file.Sync();
		BNodeInfo(&file).SetType("text/calendar");
		return BEntry(&directory, name.c_str()).GetRef(created);
	}
	return B_NAME_IN_USE;
}


status_t
ReplaceText(const entry_ref& file, const std::string& text)
{
	BFile out(&file, B_WRITE_ONLY | B_ERASE_FILE);
	if (out.InitCheck() != B_OK)
		return out.InitCheck();
	return out.Write(text.data(), text.size()) == (ssize_t)text.size() ? B_OK : B_IO_ERROR;
}

}	// namespace store
}	// namespace calendar
