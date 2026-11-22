/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "IcsWrite.h"

#include <set>
#include <vector>

namespace ics {

namespace {

/** a value that was decoded may have real line breaks: they are the escape `\n` in the file */
std::string
OneLine(const std::string& value)
{
	std::string out;
	out.reserve(value.size());
	for (size_t i = 0; i < value.size(); i++) {
		const char c = value[i];
		if (c == '\r') {
			if (i + 1 < value.size() && value[i + 1] == '\n')
				i++;
			out += "\\n";
		} else if (c == '\n') {
			out += "\\n";
		} else {
			out += c;
		}
	}
	return out;
}

/** the line cut after 75 bytes: the rest goes on in lines that start with a blank (and have 74 bytes of it) */
void
Fold(const std::string& line, std::string* out)
{
	size_t start = 0, room = 75;
	while (line.size() - start > room) {
		size_t cut = start + room;
		while (cut > start && ((unsigned char)line[cut] & 0xC0) == 0x80)
			cut--;		// (not in the middle of a character)
		*out += line.substr(start, cut - start) + "\r\n ";
		start = cut;
		room = 74;
	}
	*out += line.substr(start) + "\r\n";
}

std::string
ParamText(const Param& param)
{
	std::string text = ";" + param.name + "=";
	for (size_t i = 0; i < param.values.size(); i++) {
		const std::string& value = param.values[i];
		const bool quote = value.find_first_of(":;,") != std::string::npos;
		text += (i > 0 ? "," : "") + std::string(quote ? "\"" : "") + value + (quote ? "\"" : "");
	}
	return text;
}

void
Write(const Component& component, std::string* out)
{
	Fold("BEGIN:" + component.name, out);
	for (const Property& property : component.properties) {
		std::string line = property.name;
		for (const Param& param : property.params) {
			if (param.name == "ENCODING" || param.name == "CHARSET")
				continue;		// (the value is plain text now)
			line += ParamText(param);
		}
		Fold(line + ":" + OneLine(property.value), out);
	}
	for (const Component& child : component.children)
		Write(child, out);
	Fold("END:" + component.name, out);
}

/** the zones that the properties of a component name (TZID=...), also in its sub components (an alarm has none, but a zone has) */
void
CollectZones(const Component& component, std::set<std::string>* zones)
{
	for (const Property& property : component.properties) {
		for (const Param& param : property.params) {
			if (param.name == "TZID" && !param.values.empty())
				zones->insert(param.values[0]);
		}
	}
	for (const Component& child : component.children)
		CollectZones(child, zones);
}

/** the VEVENTs of the file in the order that ReadCalendar() reads them (so that the n-th is Calendar::events[n]), and its zones */
void
Collect(const Component& component, std::vector<const Component*>* events, std::vector<const Component*>* zones)
{
	for (const Component& child : component.children) {
		if (child.name == "VEVENT")
			events->push_back(&child);
		else if (child.name == "VTIMEZONE")
			zones->push_back(&child);
		else
			Collect(child, events, zones);
	}
}

}	// namespace


std::string
Serialize(const Component& component)
{
	std::string out;
	Write(component, &out);
	return out;
}


std::string
EventFile(const Parsed& parsed, const Calendar& calendar, const std::string& key)
{
	std::vector<const Component*> all, zones;
	for (const Component& root : parsed.components) {
		if (root.name == "VEVENT")
			all.push_back(&root);
		else if (root.name == "VTIMEZONE")
			zones.push_back(&root);
		else
			Collect(root, &all, &zones);
	}
	if (all.size() != calendar.events.size())
		return std::string();		// (not the same text)

	std::vector<Component> events;
	for (size_t i = 0; i < all.size(); i++) {
		if (EventKey(calendar.events[i]) != key)
			continue;
		events.push_back(*all[i]);
		if (events.back().Find("UID") == NULL) {
			Property uid;
			uid.name = "UID";
			uid.value = key;
			events.back().properties.insert(events.back().properties.begin(), uid);
		}
	}
	if (events.empty())
		return std::string();

	Component out;
	out.name = "VCALENDAR";
	// what the calendar says about itself and about the file as a whole: its version and its producer
	for (const Component& top : parsed.components) {
		if (top.name != "VCALENDAR")
			continue;
		for (const char* name : {"VERSION", "PRODID", "CALSCALE"}) {
			if (const Property* property = top.Find(name))
				out.properties.push_back(*property);
		}
		break;
	}
	bool hasVersion = false;
	for (const Property& property : out.properties)
		hasVersion = hasVersion || property.name == "VERSION";
	if (!hasVersion) {
		Property version;
		version.name = "VERSION";
		version.value = "2.0";
		out.properties.insert(out.properties.begin(), version);
	}

	std::set<std::string> wanted;
	for (const Component& event : events)
		CollectZones(event, &wanted);
	for (const Component* zone : zones) {
		const Property* id = zone->Find("TZID");
		if (id != NULL && wanted.count(id->value) > 0)
			out.children.push_back(*zone);
	}
	for (const Component& event : events)
		out.children.push_back(event);
	return Serialize(out);
}

}	// namespace ics
