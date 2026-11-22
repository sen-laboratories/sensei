/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "IcsParser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace ics {

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
Trimmed(const std::string& text)
{
	size_t first = 0, last = text.size();
	while (first < last && isspace((unsigned char)text[first]))
		first++;
	while (last > first && isspace((unsigned char)text[last - 1]))
		last--;
	return text.substr(first, last - first);
}

/** the position of the first character that is not inside double quotes */
size_t
FindOutsideQuotes(const std::string& text, char wanted, size_t from = 0)
{
	bool quoted = false;
	for (size_t i = from; i < text.size(); i++) {
		if (text[i] == '"')
			quoted = !quoted;
		else if (text[i] == wanted && !quoted)
			return i;
	}
	return std::string::npos;
}

std::vector<std::string>
SplitOutsideQuotes(const std::string& text, char separator)
{
	std::vector<std::string> parts;
	size_t at = 0;
	for (;;) {
		const size_t end = FindOutsideQuotes(text, separator, at);
		if (end == std::string::npos) {
			parts.push_back(text.substr(at));
			break;
		}
		parts.push_back(text.substr(at, end - at));
		at = end + 1;
	}
	return parts;
}

std::string
Unquoted(const std::string& text)
{
	std::string out = Trimmed(text);
	if (out.size() >= 2 && out.front() == '"' && out.back() == '"')
		out = out.substr(1, out.size() - 2);
	return out;
}

/** quoted-printable: `=41` is a byte; a `=` that is no escape stays */
std::string
DecodeQuotedPrintable(const std::string& text)
{
	std::string out;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '=' && i + 2 < text.size() + 0 && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2])) {
			out += (char)strtol(text.substr(i + 1, 2).c_str(), NULL, 16);
			i += 2;
		} else {
			out += text[i];
		}
	}
	return out;
}

/** whether the logical line so far says that its value is quoted-printable (a soft line break `=` at the end of a line goes on in the next one) */
bool
IsQuotedPrintableLine(const std::string& line)
{
	const size_t colon = FindOutsideQuotes(line, ':');
	const std::string head = Upper(line.substr(0, colon == std::string::npos ? line.size() : colon));
	return head.find("QUOTED-PRINTABLE") != std::string::npos;
}

void
AppendCodePoint(std::string* out, unsigned code)
{
	if (code < 0x80) {
		*out += (char)code;
	} else if (code < 0x800) {
		*out += (char)(0xC0 | (code >> 6));
		*out += (char)(0x80 | (code & 0x3F));
	} else {
		*out += (char)(0xE0 | (code >> 12));
		*out += (char)(0x80 | ((code >> 6) & 0x3F));
		*out += (char)(0x80 | (code & 0x3F));
	}
}

}	// namespace


bool
IsUtf8(const std::string& text)
{
	size_t i = 0;
	while (i < text.size()) {
		const unsigned char c = (unsigned char)text[i];
		size_t more = 0;
		if (c < 0x80)
			more = 0;
		else if ((c & 0xE0) == 0xC0 && c >= 0xC2)
			more = 1;
		else if ((c & 0xF0) == 0xE0)
			more = 2;
		else if ((c & 0xF8) == 0xF0 && c <= 0xF4)
			more = 3;
		else
			return false;
		if (i + more >= text.size() + (more == 0 ? 1 : 0) && more > 0 && i + more > text.size() - 1 + 0 && i + more >= text.size())
			return false;
		for (size_t k = 1; k <= more; k++) {
			if (((unsigned char)text[i + k] & 0xC0) != 0x80)
				return false;
		}
		i += more + 1;
	}
	return true;
}


std::string
Latin1ToUtf8(const std::string& text)
{
	// the characters of Windows-1252 at 0x80 to 0x9F (ISO-8859-1 has controls there, which nobody means)
	static const unsigned cp1252[32] = {
		0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
		0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
	std::string out;
	for (unsigned char c : text) {
		if (c >= 0x80 && c < 0xA0)
			AppendCodePoint(&out, cp1252[c - 0x80]);
		else
			AppendCodePoint(&out, c);
	}
	return out;
}


const std::string*
Property::ParamValue(const char* wanted) const
{
	const std::string key = Upper(wanted);
	for (const Param& param : params) {
		if (param.name == key && !param.values.empty())
			return &param.values[0];
	}
	return NULL;
}


const Property*
Component::Find(const char* wanted) const
{
	for (const Property& property : properties) {
		if (property.name == wanted)
			return &property;
	}
	return NULL;
}


std::vector<const Property*>
Component::FindAll(const char* wanted) const
{
	std::vector<const Property*> found;
	for (const Property& property : properties) {
		if (property.name == wanted)
			found.push_back(&property);
	}
	return found;
}


std::vector<const Component*>
Component::Children(const char* wanted) const
{
	std::vector<const Component*> found;
	for (const Component& child : children) {
		if (child.name == wanted)
			found.push_back(&child);
	}
	return found;
}


std::string
UnescapeText(const std::string& value)
{
	std::string out;
	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] == '\\' && i + 1 < value.size()) {
			i++;
			out += (value[i] == 'n' || value[i] == 'N') ? '\n' : value[i];
		} else {
			out += value[i];
		}
	}
	return out;
}


std::vector<std::string>
SplitEscaped(const std::string& value, char separator)
{
	std::vector<std::string> parts;
	std::string part;
	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] == '\\' && i + 1 < value.size()) {
			part += value[i];
			part += value[++i];
		} else if (value[i] == separator) {
			parts.push_back(part);
			part.clear();
		} else {
			part += value[i];
		}
	}
	parts.push_back(part);
	return parts;
}


bool
LooksLikeCalendar(const std::string& head)
{
	std::string text = head;
	if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
		text = text.substr(3);
	const std::string upper = Upper(text.substr(0, 4096));
	// (the first real line is BEGIN:VCALENDAR; a loose BEGIN:VEVENT is read as well)
	const size_t calendar = upper.find("BEGIN:VCALENDAR");
	const size_t event = upper.find("BEGIN:VEVENT");
	if (calendar == std::string::npos && event == std::string::npos)
		return false;
	const size_t at = std::min(calendar, event);
	for (size_t i = 0; i < at; i++) {
		// nothing but blanks before it
		if (!isspace((unsigned char)upper[i]))
			return false;
	}
	return true;
}


namespace {

/** one logical line into a property; false if it is none (no colon) */
bool
ParseLine(const std::string& line, Property* property, std::vector<std::string>* notes)
{
	const size_t colon = FindOutsideQuotes(line, ':');
	if (colon == std::string::npos) {
		if (notes != NULL && !Trimmed(line).empty())
			notes->push_back("a line without a colon was left out: " + Trimmed(line).substr(0, 40));
		return false;
	}
	std::vector<std::string> head = SplitOutsideQuotes(line.substr(0, colon), ';');
	std::string name = Upper(Trimmed(head[0]));
	// (a group: `item1.TEL`)
	const size_t dot = name.find('.');
	if (dot != std::string::npos)
		name = name.substr(dot + 1);
	if (name.empty()) {
		if (notes != NULL)
			notes->push_back("a line without a name was left out");
		return false;
	}
	property->name = name;
	property->value = line.substr(colon + 1);
	property->params.clear();

	std::string encoding, charset;
	for (size_t i = 1; i < head.size(); i++) {
		const std::string text = Trimmed(head[i]);
		if (text.empty())
			continue;
		const size_t equals = FindOutsideQuotes(text, '=');
		Param param;
		if (equals == std::string::npos) {
			// a parameter without a name: an encoding, a character set or a type (vCalendar 1.0, vCard 2.1)
			const std::string word = Upper(text);
			if (word == "QUOTED-PRINTABLE" || word == "BASE64" || word == "8BIT" || word == "7BIT" || word == "B" || word == "Q") {
				param.name = "ENCODING";
				param.values.push_back(word == "B" ? "BASE64" : word == "Q" ? "QUOTED-PRINTABLE" : word);
			} else if (word.compare(0, 5, "UTF-8") == 0 || word.compare(0, 8, "ISO-8859") == 0 || word.compare(0, 7, "WINDOWS") == 0 || word == "US-ASCII") {
				param.name = "CHARSET";
				param.values.push_back(word);
			} else {
				param.name = "TYPE";
				param.values.push_back(text);
			}
		} else {
			param.name = Upper(Trimmed(text.substr(0, equals)));
			for (const std::string& each : SplitOutsideQuotes(text.substr(equals + 1), ','))
				param.values.push_back(Unquoted(each));
		}
		if (param.name == "ENCODING" && !param.values.empty())
			encoding = Upper(param.values[0]);
		if (param.name == "CHARSET" && !param.values.empty())
			charset = Upper(param.values[0]);
		property->params.push_back(param);
	}

	if (encoding == "QUOTED-PRINTABLE")
		property->value = DecodeQuotedPrintable(property->value);
	else if (encoding == "BASE64")
		property->value.clear();		// (an attachment: not read)
	// the character set: what is not UTF-8 is taken as Windows-1252, whatever it says
	const bool latin = charset.compare(0, 8, "ISO-8859") == 0 || charset.compare(0, 7, "WINDOWS") == 0;
	if ((latin && !IsUtf8(property->value)) || (!IsUtf8(property->value) && !property->value.empty()))
		property->value = Latin1ToUtf8(property->value);
	else if (latin && charset != "ISO-8859-1" && charset != "WINDOWS-1252" && IsUtf8(property->value))
		;		// (some other set whose text happens to be valid UTF-8: left as it is)
	// (a value that says ISO-8859-1 but is valid UTF-8 was most likely written as UTF-8: left as it is)
	return true;
}

}	// namespace


Parsed
Parse(const std::string& input)
{
	Parsed result;
	std::string text = input;
	if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
		text = text.substr(3);

	// the physical lines
	std::vector<std::string> lines;
	{
		std::string line;
		for (size_t i = 0; i < text.size(); i++) {
			const char c = text[i];
			if (c == '\r' || c == '\n') {
				if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
					i++;
				lines.push_back(line);
				line.clear();
			} else {
				line += c;
			}
		}
		if (!line.empty())
			lines.push_back(line);
	}

	// the logical lines: a line that starts with a blank goes on the one before; a quoted-printable line that ends in `=` goes on with the next one
	std::vector<std::string> logical;
	bool softBreak = false;
	for (const std::string& physical : lines) {
		if (!logical.empty() && softBreak) {
			logical.back().pop_back();		// (the =)
			logical.back() += physical;
		} else if (!logical.empty() && !physical.empty() && (physical[0] == ' ' || physical[0] == '\t')) {
			logical.back() += physical.substr(1);
		} else {
			logical.push_back(physical);
		}
		softBreak = !logical.back().empty() && logical.back().back() == '=' && IsQuotedPrintableLine(logical.back());
	}

	// the components
	std::vector<Component> stack;		// the open ones, the innermost last
	auto close = [&]() {
		Component done = stack.back();
		stack.pop_back();
		if (stack.empty())
			result.components.push_back(done);
		else
			stack.back().children.push_back(done);
	};
	for (const std::string& line : logical) {
		if (Trimmed(line).empty())
			continue;
		Property property;
		if (!ParseLine(line, &property, &result.notes))
			continue;
		const std::string value = Upper(Trimmed(property.value));
		if (property.name == "BEGIN") {
			// (a component never holds one of its own name: the one before was not closed, this is its next sibling)
			if (!stack.empty() && stack.back().name == value) {
				result.notes.push_back("a component that was not closed: " + value);
				close();
			}
			Component component;
			component.name = value;
			stack.push_back(component);
		} else if (property.name == "END") {
			// the innermost open component of that name ends, and the ones inside it with it
			int found = -1;
			for (int i = (int)stack.size() - 1; i >= 0; i--) {
				if (stack[i].name == value) {
					found = i;
					break;
				}
			}
			if (found < 0) {
				result.notes.push_back("END:" + value + " matches nothing");
				continue;
			}
			while ((int)stack.size() > found + 1) {
				result.notes.push_back("a component that was not closed: " + stack.back().name);
				close();
			}
			close();
		} else if (stack.empty()) {
			result.notes.push_back("a property outside a component was left out: " + property.name);
		} else {
			stack.back().properties.push_back(property);
		}
	}
	while (!stack.empty()) {
		result.notes.push_back("a component that was not closed: " + stack.back().name);
		close();
	}
	return result;
}

}	// namespace ics
