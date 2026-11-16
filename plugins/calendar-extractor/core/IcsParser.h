/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

/**
 * @file IcsParser.h
 * @brief A forgiving reader for the text of iCalendar (RFC 5545) and vCalendar 1.0 files: lines, properties with parameters, nested components. Plain C++.
 *
 * What it forgives: line ends of any kind (CRLF, LF, CR), a byte order mark, folded lines with a tab or blanks, a line that has no colon (left out), components that are not closed (closed at
 * the end of the file or by an END of an outer one), an END that matches nothing (ignored), properties outside any component (ignored), lower case names, a group before a name (`item1.`),
 * parameters without a name (`;QUOTED-PRINTABLE`, `;UTF-8`, `;WORK` of vCalendar 1.0 and vCard 2.1), quoted-printable values that go over several lines, and text that is not UTF-8
 * (taken as Windows-1252 and converted). Nothing it meets makes it fail: what it could not read is told in the notes.
 */

namespace ics {

struct Param {
	std::string					name;		///< upper case
	std::vector<std::string>	values;
};

struct Property {
	std::string			name;		///< upper case, without the group
	std::string			value;		///< as written, decoded (quoted-printable) and in UTF-8; the escapes of text (`\n`, `\,`) are still in it: see UnescapeText()
	std::vector<Param>	params;

	/** the first value of a parameter (the name in any case), or NULL */
	const std::string*	ParamValue(const char* name) const;
};

struct Component {
	std::string				name;		///< upper case: VCALENDAR, VEVENT, VTIMEZONE, STANDARD...
	std::vector<Property>	properties;
	std::vector<Component>	children;

	const Property*					Find(const char* name) const;
	std::vector<const Property*>	FindAll(const char* name) const;
	std::vector<const Component*>	Children(const char* name) const;
};

struct Parsed {
	std::vector<Component>		components;		///< what is at the top: normally one VCALENDAR; also loose VEVENTs
	std::vector<std::string>	notes;			///< what could not be read, for the log
};

/** Reads the whole text. */
Parsed Parse(const std::string& text);

/** Whether the beginning of a file looks like a calendar (BEGIN:VCALENDAR, or BEGIN:VEVENT, after blanks and a byte order mark): for identify. */
bool LooksLikeCalendar(const std::string& head);

/** The text of a TEXT value: `\n` and `\N` are a line break, `\,` `\;` `\\` the character itself. */
std::string UnescapeText(const std::string& value);

/** The parts of a value that are separated by @p separator, not counting an escaped one (`\,`); the parts are not unescaped. */
std::vector<std::string> SplitEscaped(const std::string& value, char separator);

/** Text that is Windows-1252 or ISO-8859-1 as UTF-8. */
std::string Latin1ToUtf8(const std::string& text);
/** Whether the text is valid UTF-8. */
bool IsUtf8(const std::string& text);

}	// namespace ics
