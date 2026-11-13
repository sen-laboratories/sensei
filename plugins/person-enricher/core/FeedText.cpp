/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "FeedText.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace robin {
namespace feedtext {

namespace {

void
AppendUtf8(std::string* out, unsigned long code)
{
	if (code < 0x80) {
		*out += (char)code;
	} else if (code < 0x800) {
		*out += (char)(0xC0 | (code >> 6));
		*out += (char)(0x80 | (code & 0x3F));
	} else if (code < 0x10000) {
		*out += (char)(0xE0 | (code >> 12));
		*out += (char)(0x80 | ((code >> 6) & 0x3F));
		*out += (char)(0x80 | (code & 0x3F));
	} else if (code < 0x110000) {
		*out += (char)(0xF0 | (code >> 18));
		*out += (char)(0x80 | ((code >> 12) & 0x3F));
		*out += (char)(0x80 | ((code >> 6) & 0x3F));
		*out += (char)(0x80 | (code & 0x3F));
	}
}


struct Named {
	const char*		name;
	unsigned long	code;
};

const Named kNamed[] = {
	{"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"ndash", 0x2013}, {"mdash", 0x2014},
	{"hellip", 0x2026}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"laquo", 0xAB}, {"raquo", 0xBB},
	{"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"bull", 0x2022}, {"middot", 0xB7}, {"euro", 0x20AC}, {"shy", 0xAD},
};


bool
IsSpace(char c)
{
	return std::isspace((unsigned char)c) != 0;
}


std::string
Lower(std::string_view text)
{
	std::string lower(text);
	for (char& c : lower)
		c = (char)std::tolower((unsigned char)c);
	return lower;
}


// the value of an attribute in the text of a tag (`<a href="x">`); empty if there is none
std::string
Attribute(const std::string& tag, const char* name)
{
	const std::string lower = Lower(tag);
	size_t from = 0;
	while ((from = lower.find(name, from)) != std::string::npos) {
		const bool boundary = from == 0 || IsSpace(lower[from - 1]) || lower[from - 1] == '<';
		size_t at = from + strlen(name);
		from = at;
		while (at < lower.size() && IsSpace(lower[at]))
			at++;
		if (!boundary || at >= lower.size() || lower[at] != '=')
			continue;
		at++;
		while (at < lower.size() && IsSpace(lower[at]))
			at++;
		if (at >= tag.size())
			return std::string();
		if (tag[at] == '"' || tag[at] == '\'') {
			const size_t end = tag.find(tag[at], at + 1);
			return end == std::string::npos ? std::string() : tag.substr(at + 1, end - at - 1);
		}
		size_t end = at;
		while (end < tag.size() && !IsSpace(tag[end]) && tag[end] != '>')
			end++;
		return tag.substr(at, end - at);
	}
	return std::string();
}


// The tag that starts at `open` (the position of the `<`): its end (after the `>`), its lower case name, whether it closes (`</p>`), and its text. A `<` that is no tag (`a < b`) is not one.
bool
ReadTag(std::string_view html, size_t open, size_t* end, std::string* name, bool* closing, std::string* text)
{
	size_t at = open + 1;
	if (at >= html.size())
		return false;
	if (html.compare(open, 4, "<!--") == 0) {
		const size_t stop = html.find("-->", open + 4);
		*end = stop == std::string_view::npos ? html.size() : stop + 3;
		name->clear();
		*closing = false;
		text->clear();
		return true;
	}
	*closing = html[at] == '/';
	if (*closing)
		at++;
	if (at >= html.size() || !(std::isalpha((unsigned char)html[at]) || html[at] == '!' || html[at] == '?'))
		return false;
	size_t nameEnd = at;
	while (nameEnd < html.size() && !IsSpace(html[nameEnd]) && html[nameEnd] != '>' && html[nameEnd] != '/')
		nameEnd++;
	*name = Lower(html.substr(at, nameEnd - at));

	// (a `>` inside a quoted value does not end the tag)
	char quote = 0;
	size_t stop = nameEnd;
	for (; stop < html.size(); stop++) {
		const char c = html[stop];
		if (quote != 0) {
			if (c == quote)
				quote = 0;
		} else if (c == '"' || c == '\'') {
			quote = c;
		} else if (c == '>') {
			break;
		}
	}
	*end = stop < html.size() ? stop + 1 : html.size();
	*text = std::string(html.substr(open, *end - open));
	return true;
}


void
CollapseSpace(std::string* line)
{
	std::string clean;
	bool lastSpace = true;
	for (char c : *line) {
		if (IsSpace(c)) {
			if (!lastSpace) {
				clean += ' ';
				lastSpace = true;
			}
		} else {
			clean += c;
			lastSpace = false;
		}
	}
	if (!clean.empty() && clean.back() == ' ')
		clean.pop_back();
	*line = clean;
}


// the text up to `max` bytes, ended at a character
std::string
CutUtf8(const std::string& text, size_t max)
{
	if (text.size() <= max)
		return text;
	size_t cut = max;
	while (cut > 0 && ((unsigned char)text[cut] & 0xC0) == 0x80)
		cut--;
	return text.substr(0, cut);
}


// days since 1970-01-01 of a day of the proleptic Gregorian calendar
int64_t
DaysFromCivil(int64_t year, unsigned month, unsigned day)
{
	year -= month <= 2;
	const int64_t era = (year >= 0 ? year : year - 399) / 400;
	const unsigned yoe = (unsigned)(year - era * 400);
	const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}


void
CivilFromDays(int64_t days, int* year, unsigned* month, unsigned* day)
{
	days += 719468;
	const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	const unsigned doe = (unsigned)(days - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t y = (int64_t)yoe + era * 400;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	*day = doy - (153 * mp + 2) / 5 + 1;
	*month = mp < 10 ? mp + 3 : mp - 9;
	*year = (int)(y + (*month <= 2));
}


bool
ReadNumber(const std::string& text, size_t* at, int digitsMin, int digitsMax, int* value)
{
	size_t start = *at;
	int result = 0;
	while (*at < text.size() && std::isdigit((unsigned char)text[*at]) && (int)(*at - start) < digitsMax)
		result = result * 10 + (text[(*at)++] - '0');
	if ((int)(*at - start) < digitsMin)
		return false;
	*value = result;
	return true;
}


int
MonthOf(const std::string& name)
{
	static const char* months[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
	const std::string lower = Lower(name);
	for (int i = 0; i < 12; i++) {
		if (lower.compare(0, 3, months[i]) == 0)
			return i + 1;
	}
	return 0;
}


// the offset of a zone in seconds: `Z`, `GMT`, `+0100`, `-05:00`, `EST`...; false if it is none
bool
ReadZone(const std::string& text, size_t at, int* seconds)
{
	while (at < text.size() && IsSpace(text[at]))
		at++;
	*seconds = 0;
	if (at >= text.size())
		return true;
	if (text[at] == '+' || text[at] == '-') {
		const int sign = text[at] == '-' ? -1 : 1;
		at++;
		int hours = 0, minutes = 0;
		if (!ReadNumber(text, &at, 2, 2, &hours))
			return false;
		if (at < text.size() && text[at] == ':')
			at++;
		if (at < text.size())
			ReadNumber(text, &at, 2, 2, &minutes);
		*seconds = sign * (hours * 3600 + minutes * 60);
		return true;
	}
	std::string zone;
	while (at < text.size() && std::isalpha((unsigned char)text[at]))
		zone += (char)std::toupper((unsigned char)text[at++]);
	static const struct { const char* name; int hours; } zones[] = {
		{"Z", 0}, {"UT", 0}, {"UTC", 0}, {"GMT", 0}, {"EST", -5}, {"EDT", -4}, {"CST", -6}, {"CDT", -5}, {"MST", -7}, {"MDT", -6}, {"PST", -8}, {"PDT", -7},
	};
	for (const auto& known : zones) {
		if (zone == known.name) {
			*seconds = known.hours * 3600;
			return true;
		}
	}
	return zone.empty();
}

}	// namespace


std::string
UnwrapCdata(std::string_view text)
{
	std::string result;
	size_t at = 0;
	while (at < text.size()) {
		const size_t open = text.find("<![CDATA[", at);
		if (open == std::string_view::npos) {
			result += text.substr(at);
			break;
		}
		result += text.substr(at, open - at);
		const size_t start = open + 9;
		const size_t close = text.find("]]>", start);
		if (close == std::string_view::npos) {
			result += text.substr(start);
			break;
		}
		result += text.substr(start, close - start);
		at = close + 3;
	}
	return result;
}


std::string
DecodeEntities(std::string_view text)
{
	std::string result;
	result.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != '&') {
			result += text[i];
			continue;
		}
		const size_t semicolon = text.find(';', i + 1);
		if (semicolon == std::string_view::npos || semicolon - i > 10) {
			result += '&';
			continue;
		}
		const std::string entity(text.substr(i + 1, semicolon - i - 1));
		unsigned long code = 0;
		bool known = false;
		if (!entity.empty() && entity[0] == '#') {
			char* end = NULL;
			code = (entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')) ? strtoul(entity.c_str() + 2, &end, 16) : strtoul(entity.c_str() + 1, &end, 10);
			known = end != NULL && *end == '\0' && entity.size() > 1 && code > 0 && code < 0x110000;
		} else {
			for (const Named& named : kNamed) {
				if (entity == named.name) {
					code = named.code;
					known = true;
					break;
				}
			}
		}
		if (!known) {
			result += '&';
			continue;
		}
		AppendUtf8(&result, code);
		i = semicolon;
	}
	return result;
}


std::string
HtmlToPlainText(std::string_view html)
{
	const std::string source = UnwrapCdata(html);
	std::string text;
	for (size_t i = 0; i < source.size();) {
		if (source[i] == '<') {
			size_t end = 0;
			std::string name, tag;
			bool closing = false;
			if (ReadTag(source, i, &end, &name, &closing, &tag)) {
				// (the end of a paragraph or a line is a space, so that two words do not run together)
				if (name == "br" || name == "p" || name == "div" || name == "li" || name == "tr")
					text += ' ';
				i = end;
				continue;
			}
		}
		text += source[i++];
	}
	std::string plain = DecodeEntities(text);
	CollapseSpace(&plain);
	return plain;
}


std::string
HtmlToMarkdown(std::string_view html)
{
	const std::string source = UnwrapCdata(html);
	std::string out;
	struct Link {
		size_t		start;
		std::string	href;
	};
	std::vector<Link> links;
	std::string pending;		// text that was not decoded and folded yet

	auto flush = [&]() {
		if (pending.empty())
			return;
		std::string text = DecodeEntities(pending);
		pending.clear();
		// (white space in HTML is one space, at the start of a line none)
		std::string folded;
		for (char c : text) {
			if (IsSpace(c) && c != '\xA0') {
				if (!folded.empty() && folded.back() != ' ' && folded.back() != '\n')
					folded += ' ';
				else if (folded.empty() && !out.empty() && out.back() != ' ' && out.back() != '\n')
					folded += ' ';
			} else {
				folded += c;
			}
		}
		out += folded;
	};
	auto blankLine = [&]() {
		flush();
		while (!out.empty() && out.back() == ' ')
			out.pop_back();
		if (out.empty())
			return;
		while (out.size() < 2 || out.compare(out.size() - 2, 2, "\n\n") != 0)
			out += '\n';
	};
	auto newLine = [&]() {
		flush();
		while (!out.empty() && out.back() == ' ')
			out.pop_back();
		if (!out.empty() && out.back() != '\n')
			out += '\n';
	};

	for (size_t i = 0; i < source.size();) {
		if (source[i] == '<') {
			size_t end = 0;
			std::string name, tag;
			bool closing = false;
			if (ReadTag(source, i, &end, &name, &closing, &tag)) {
				flush();
				if (name == "p" || name == "div" || name == "blockquote" || name == "pre" || name == "ul" || name == "ol") {
					blankLine();
				} else if (name == "br") {
					newLine();
				} else if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
					blankLine();
					if (!closing)
						out += std::string((size_t)(name[1] - '0'), '#') + " ";
				} else if (name == "li") {
					if (!closing) {
						newLine();
						out += "- ";
					}
				} else if (name == "strong" || name == "b") {
					out += "**";
				} else if (name == "em" || name == "i") {
					out += "*";
				} else if (name == "code") {
					out += "`";
				} else if (name == "a") {
					if (!closing) {
						links.push_back(Link{out.size(), Attribute(tag, "href")});
					} else if (!links.empty()) {
						const Link link = links.back();
						links.pop_back();
						std::string label = out.substr(link.start);
						out.resize(link.start);
						while (!label.empty() && IsSpace(label.back()))
							label.pop_back();
						if (link.href.empty())
							out += label;
						else
							out += "[" + (label.empty() ? link.href : label) + "](" + DecodeEntities(link.href) + ")";
					}
				} else if (name == "img" && !closing) {
					const std::string src = DecodeEntities(Attribute(tag, "src"));
					if (!src.empty())
						out += "![" + DecodeEntities(Attribute(tag, "alt")) + "](" + src + ")";
				}
				i = end;
				continue;
			}
		}
		pending += source[i++];
	}
	flush();
	while (!links.empty()) {		// (a link that was never closed is its text)
		links.pop_back();
	}
	while (!out.empty() && (IsSpace(out.back())))
		out.pop_back();
	// (no more than one empty line)
	std::string clean;
	int newlines = 0;
	for (char c : out) {
		if (c == '\n') {
			if (++newlines <= 2)
				clean += c;
		} else {
			newlines = 0;
			clean += c;
		}
	}
	size_t first = 0;
	while (first < clean.size() && IsSpace(clean[first]))
		first++;
	return clean.substr(first);
}


std::string
TruncateUtf8(const std::string& text, size_t maxBytes)
{
	if (text.size() <= maxBytes)
		return text;
	if (maxBytes < 4)
		return CutUtf8(text, maxBytes);
	std::string cut = CutUtf8(text, maxBytes - 3);
	while (!cut.empty() && IsSpace(cut.back()))
		cut.pop_back();
	return cut + "\xE2\x80\xA6";
}


bool
ParseDate(const std::string& text, int64_t* epoch)
{
	size_t at = 0;
	while (at < text.size() && IsSpace(text[at]))
		at++;
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, zone = 0;

	if (at + 4 <= text.size() && std::isdigit((unsigned char)text[at]) && std::isdigit((unsigned char)text[at + 1]) && std::isdigit((unsigned char)text[at + 2])
			&& std::isdigit((unsigned char)text[at + 3])) {
		// ISO 8601
		if (!ReadNumber(text, &at, 4, 4, &year) || at >= text.size() || text[at] != '-')
			return false;
		at++;
		if (!ReadNumber(text, &at, 2, 2, &month) || at >= text.size() || text[at] != '-')
			return false;
		at++;
		if (!ReadNumber(text, &at, 2, 2, &day))
			return false;
		if (at < text.size() && (text[at] == 'T' || text[at] == ' ')) {
			at++;
			if (!ReadNumber(text, &at, 2, 2, &hour) || at >= text.size() || text[at] != ':')
				return false;
			at++;
			if (!ReadNumber(text, &at, 2, 2, &minute))
				return false;
			if (at < text.size() && text[at] == ':') {
				at++;
				if (!ReadNumber(text, &at, 2, 2, &second))
					return false;
				if (at < text.size() && (text[at] == '.' || text[at] == ',')) {
					at++;
					while (at < text.size() && std::isdigit((unsigned char)text[at]))
						at++;
				}
			}
			if (!ReadZone(text, at, &zone))
				return false;
		}
	} else {
		// RFC 822: [Fri,] 06 Nov 2026 22:15:59 +0000
		const size_t comma = text.find(',', at);
		if (comma != std::string::npos && comma - at <= 4)
			at = comma + 1;
		while (at < text.size() && IsSpace(text[at]))
			at++;
		if (!ReadNumber(text, &at, 1, 2, &day))
			return false;
		while (at < text.size() && IsSpace(text[at]))
			at++;
		std::string name;
		while (at < text.size() && std::isalpha((unsigned char)text[at]))
			name += text[at++];
		month = MonthOf(name);
		if (month == 0)
			return false;
		while (at < text.size() && IsSpace(text[at]))
			at++;
		const size_t yearStart = at;
		if (!ReadNumber(text, &at, 2, 4, &year))
			return false;
		if (at - yearStart == 2)
			year += year < 50 ? 2000 : 1900;
		while (at < text.size() && IsSpace(text[at]))
			at++;
		if (at < text.size() && std::isdigit((unsigned char)text[at])) {
			if (!ReadNumber(text, &at, 1, 2, &hour) || at >= text.size() || text[at] != ':')
				return false;
			at++;
			if (!ReadNumber(text, &at, 2, 2, &minute))
				return false;
			if (at < text.size() && text[at] == ':') {
				at++;
				if (!ReadNumber(text, &at, 2, 2, &second))
					return false;
			}
			if (!ReadZone(text, at, &zone))
				return false;
		}
	}

	if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
		return false;
	// (a day that is not in the calendar: the 31st of April)
	static const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	if (day > lengths[month - 1] + (month == 2 && leap ? 1 : 0))
		return false;
	*epoch = DaysFromCivil(year, (unsigned)month, (unsigned)day) * 86400 + hour * 3600 + minute * 60 + second - zone;
	return true;
}


std::string
FormatIso(int64_t epoch)
{
	int64_t days = epoch / 86400;
	int64_t rest = epoch % 86400;
	if (rest < 0) {
		rest += 86400;
		days--;
	}
	int year = 0;
	unsigned month = 0, day = 0;
	CivilFromDays(days, &year, &month, &day);
	char text[40];
	snprintf(text, sizeof(text), "%04d-%02u-%02uT%02d:%02d:%02dZ", year, month, day, (int)(rest / 3600), (int)(rest % 3600 / 60), (int)(rest % 60));
	return text;
}


std::string
FileDatePrefix(int64_t epoch)
{
	const std::string iso = FormatIso(epoch);		// 2026-11-08T18:00:00Z
	return iso.substr(0, 10) + " " + iso.substr(11, 2) + "." + iso.substr(14, 2);
}


std::string
FileNamePart(const std::string& text, size_t maxBytes)
{
	std::string clean;
	for (char c : text) {
		if (c == '/' || c == ':')
			clean += '-';
		else if ((unsigned char)c < 0x20 || c == 0x7F)
			clean += ' ';
		else
			clean += c;
	}
	CollapseSpace(&clean);
	clean = CutUtf8(clean, maxBytes);
	while (!clean.empty() && (IsSpace(clean.back()) || clean.back() == '.'))
		clean.pop_back();
	return clean;
}


std::string
WebUrl(const std::string& url, const std::string& handle)
{
	if (url.compare(0, 5, "at://") != 0)
		return url;
	std::vector<std::string> parts;
	std::string part;
	for (size_t i = 5; i <= url.size(); i++) {
		if (i == url.size() || url[i] == '/') {
			parts.push_back(part);
			part.clear();
		} else {
			part += url[i];
		}
	}
	if (parts.empty() || parts[0].empty())
		return std::string();
	const std::string who = handle.empty() ? parts[0] : handle;
	if (parts.size() >= 3 && parts[1] == "app.bsky.feed.post" && !parts[2].empty())
		return "https://bsky.app/profile/" + who + "/post/" + parts[2];
	if (parts.size() == 1)
		return "https://bsky.app/profile/" + who;
	return std::string();
}

}	// namespace feedtext
}	// namespace robin
