/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperString.h"

#include <cctype>
#include <cstdlib>

namespace paperboy {

/** blanks and line breaks in a row are one blank, nothing at the ends (arXiv breaks the lines of titles and abstracts) */
std::string
SqueezeBlanks(const std::string& text)
{
	std::string out;
	bool blank = true;
	for (unsigned char c : text) {
		if (std::isspace(c)) {
			blank = true;
			continue;
		}
		if (blank && !out.empty())
			out += ' ';
		blank = false;
		out += (char) c;
	}
	return out;
}

static void
AppendUtf8(std::string* out, unsigned long code)
{
	if (code < 0x80) {
		*out += (char) code;
	} else if (code < 0x800) {
		*out += (char) (0xC0 | (code >> 6));
		*out += (char) (0x80 | (code & 0x3F));
	} else if (code < 0x10000) {
		*out += (char) (0xE0 | (code >> 12));
		*out += (char) (0x80 | ((code >> 6) & 0x3F));
		*out += (char) (0x80 | (code & 0x3F));
	} else {
		*out += (char) (0xF0 | (code >> 18));
		*out += (char) (0x80 | ((code >> 12) & 0x3F));
		*out += (char) (0x80 | ((code >> 6) & 0x3F));
		*out += (char) (0x80 | (code & 0x3F));
	}
}

/** the five entities of XML and the numeric ones */
std::string
DecodeEntities(const std::string& text)
{
	std::string out;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != '&') {
			out += text[i];
			continue;
		}
		size_t end = text.find(';', i);
		if (end == std::string::npos || end - i > 10) {
			out += text[i];
			continue;
		}
		const std::string name = text.substr(i + 1, end - i - 1);
		if (name == "amp") out += '&';
		else if (name == "lt") out += '<';
		else if (name == "gt") out += '>';
		else if (name == "quot") out += '"';
		else if (name == "apos") out += '\'';
		else if (name.size() > 1 && name[0] == '#') {
			const bool hex = name[1] == 'x' || name[1] == 'X';
			char* parsed = nullptr;
			unsigned long code = std::strtoul(name.c_str() + (hex ? 2 : 1), &parsed, hex ? 16 : 10);
			if (parsed != nullptr && *parsed == '\0' && code > 0 && code < 0x110000)
				AppendUtf8(&out, code);
			else
				out += text.substr(i, end - i + 1);
		} else {
			out += text.substr(i, end - i + 1);
		}
		i = end;
	}
	return out;
}


std::string
StripTags(const std::string& html)
{
	std::string text;
	bool inTag = false;
	for (char c : html) {
		if (c == '<')
			inTag = true;
		else if (c == '>' && inTag)
			inTag = false;
		else if (!inTag)
			text += c;
	}
	return SqueezeBlanks(DecodeEntities(text));
}

}	// namespace paperboy
