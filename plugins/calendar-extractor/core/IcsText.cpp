/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include <sstream>
#include "IcsText.h"
#include <sen/SenRecurrence.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace ics {

namespace {

std::string
Lower(const std::string& text)
{
	std::string out = text;
	for (char& c : out)
		c = (char)tolower((unsigned char)c);
	return out;
}

void
AppendUtf8(std::string* out, unsigned code)
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
	} else {
		*out += (char)(0xF0 | (code >> 18));
		*out += (char)(0x80 | ((code >> 12) & 0x3F));
		*out += (char)(0x80 | ((code >> 6) & 0x3F));
		*out += (char)(0x80 | (code & 0x3F));
	}
}

/** `&amp;` and `&#8217;` in a text */
std::string
DecodeEntities(const std::string& text)
{
	struct Named { const char* name; unsigned code; };
	static const Named named[] = {
		{"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"ndash", 0x2013}, {"mdash", 0x2014}, {"hellip", 0x2026},
		{"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bull", 0x2022}, {"euro", 0x20AC}, {"copy", 0xA9}, {"reg", 0xAE},
		{"auml", 0xE4}, {"ouml", 0xF6}, {"uuml", 0xFC}, {"Auml", 0xC4}, {"Ouml", 0xD6}, {"Uuml", 0xDC}, {"szlig", 0xDF}};
	std::string out;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != '&') {
			out += text[i];
			continue;
		}
		const size_t end = text.find(';', i);
		if (end == std::string::npos || end - i > 10) {
			out += '&';
			continue;
		}
		const std::string name = text.substr(i + 1, end - i - 1);
		unsigned code = 0;
		bool known = false;
		if (!name.empty() && name[0] == '#') {
			code = (name.size() > 1 && (name[1] == 'x' || name[1] == 'X')) ? (unsigned)strtoul(name.c_str() + 2, NULL, 16) : (unsigned)strtoul(name.c_str() + 1, NULL, 10);
			known = code > 0 && code < 0x110000;
		} else {
			for (const Named& each : named) {
				if (name == each.name) {
					code = each.code;
					known = true;
					break;
				}
			}
		}
		if (known) {
			AppendUtf8(&out, code == 0xA0 ? ' ' : code);
			i = end;
		} else {
			out += '&';
		}
	}
	return out;
}

/** the value of an attribute in the text of a tag (`href="..."`) */
std::string
AttributeOf(const std::string& tag, const char* name)
{
	const std::string lower = Lower(tag);
	size_t at = 0;
	for (;;) {
		at = lower.find(name, at);
		if (at == std::string::npos)
			return std::string();
		const bool boundary = at > 0 && isspace((unsigned char)lower[at - 1]);
		size_t after = at + strlen(name);
		while (after < lower.size() && isspace((unsigned char)lower[after]))
			after++;
		if (boundary && after < lower.size() && lower[after] == '=') {
			after++;
			while (after < lower.size() && isspace((unsigned char)lower[after]))
				after++;
			if (after >= tag.size())
				return std::string();
			if (tag[after] == '"' || tag[after] == '\'') {
				const size_t end = tag.find(tag[after], after + 1);
				return DecodeEntities(tag.substr(after + 1, end == std::string::npos ? std::string::npos : end - after - 1));
			}
			size_t end = after;
			while (end < tag.size() && !isspace((unsigned char)tag[end]) && tag[end] != '>')
				end++;
			return DecodeEntities(tag.substr(after, end - after));
		}
		at += strlen(name);
	}
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

bool
IsHttpStart(const std::string& text, size_t at)
{
	return text.compare(at, 7, "http://") == 0 || text.compare(at, 8, "https://") == 0
		|| (text.size() >= at + 7 && Lower(text.substr(at, 8)) == "https://") || (text.size() >= at + 7 && Lower(text.substr(at, 7)) == "http://");
}

/** the end of the link that starts at @p at in a text: the sentence does not belong to it */
size_t
UrlEnd(const std::string& text, size_t at)
{
	size_t end = at;
	while (end < text.size() && !isspace((unsigned char)text[end]) && text[end] != '<' && text[end] != '>' && text[end] != '"' && text[end] != '\'' && text[end] != '[' && (unsigned char)text[end] >= 0x20)
		end++;
	// (what ends a sentence or a bracket is not part of the link)
	while (end > at) {
		const char c = text[end - 1];
		if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == '*' || c == '_')
			end--;
		else if (c == ')' && text.substr(at, end - at).find('(') == std::string::npos)
			end--;
		else if (c == ']')
			end--;
		else
			break;
	}
	return end;
}

}	// namespace


bool
LooksLikeHtml(const std::string& text)
{
	const std::string lower = Lower(text);
	static const char* tags[] = {"<br", "<p>", "<p ", "</p>", "<a ", "</a>", "<b>", "<i>", "<strong", "<em>", "<ul", "<li", "<div", "<span", "<h1", "<h2", "<h3", "<html", "<body", "<table"};
	for (const char* tag : tags) {
		if (lower.find(tag) != std::string::npos)
			return true;
	}
	return false;
}


std::string
HtmlToMarkdown(const std::string& html)
{
	std::string out;
	std::vector<std::string> lists;		// "ul" or "ol:3" (the number of the next item)
	std::string linkTarget, linkText;
	bool inLink = false, skip = false, pre = false;
	int bold = 0, italic = 0;

	auto appendText = [&](const std::string& piece) {
		if (inLink)
			linkText += piece;
		else
			out += piece;
	};
	auto endBlock = [&](int lines) {
		// (at least @p lines line ends at the end of the text, but none at its beginning)
		if (out.empty() || inLink)
			return;
		int have = 0;
		for (size_t i = out.size(); i > 0 && (out[i - 1] == '\n' || out[i - 1] == ' '); i--) {
			if (out[i - 1] == '\n')
				have++;
		}
		while (!out.empty() && out.back() == ' ')
			out.pop_back();
		for (; have < lines; have++)
			out += '\n';
	};

	for (size_t i = 0; i < html.size();) {
		if (html[i] == '<') {
			const size_t end = html.find('>', i);
			if (end == std::string::npos) {
				i++;
				continue;
			}
			std::string tag = html.substr(i + 1, end - i - 1);
			i = end + 1;
			if (tag.compare(0, 3, "!--") == 0)
				continue;
			const bool closing = !tag.empty() && tag[0] == '/';
			if (closing)
				tag = tag.substr(1);
			size_t nameEnd = 0;
			while (nameEnd < tag.size() && !isspace((unsigned char)tag[nameEnd]) && tag[nameEnd] != '/')
				nameEnd++;
			const std::string name = Lower(tag.substr(0, nameEnd));
			if (name == "script" || name == "style" || name == "head") {
				skip = !closing;
				continue;
			}
			if (skip)
				continue;
			if (name == "br") {
				if (inLink)
					linkText += ' ';
				else
					out += "  \n";
			} else if (name == "p" || name == "div" || name == "tr" || name == "table" || name == "blockquote") {
				endBlock(2);
			} else if (name == "hr") {
				endBlock(2);
				out += "---\n\n";
			} else if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
				if (!closing) {
					endBlock(2);
					out += std::string(name[1] - '0', '#') + " ";
				} else {
					endBlock(2);
				}
			} else if (name == "ul" || name == "ol") {
				if (!closing) {
					endBlock(lists.empty() ? 2 : 1);
					lists.push_back(name == "ol" ? "ol:1" : "ul");
				} else {
					if (!lists.empty())
						lists.pop_back();
					endBlock(lists.empty() ? 2 : 1);
				}
			} else if (name == "li") {
				if (!closing && !lists.empty()) {
					endBlock(1);
					std::string& kind = lists.back();
					out += std::string((lists.size() - 1) * 4, ' ');
					if (kind.compare(0, 2, "ol") == 0) {
						const int number = atoi(kind.c_str() + 3);
						out += std::to_string(number) + ". ";
						kind = "ol:" + std::to_string(number + 1);
					} else {
						out += "- ";
					}
				} else if (closing) {
					endBlock(1);
				}
			} else if (name == "b" || name == "strong") {
				bold += closing ? -1 : 1;
				if (bold == 0 || bold == 1)
					appendText("**");
			} else if (name == "i" || name == "em") {
				italic += closing ? -1 : 1;
				if (italic == 0 || italic == 1)
					appendText("*");
			} else if (name == "a") {
				if (!closing) {
					linkTarget = AttributeOf(tag, "href");
					linkText.clear();
					inLink = !linkTarget.empty();
				} else if (inLink) {
					inLink = false;
					const std::string label = Trimmed(linkText);
					if (label.empty() || label == linkTarget)
						out += "<" + linkTarget + ">";
					else
						out += "[" + label + "](" + linkTarget + ")";
				}
			} else if (name == "pre") {
				pre = !closing;
			}
			continue;
		}
		if (skip) {
			const size_t next = html.find('<', i);
			i = next == std::string::npos ? html.size() : next;
			continue;
		}
		size_t next = html.find('<', i);
		if (next == std::string::npos)
			next = html.size();
		std::string piece = DecodeEntities(html.substr(i, next - i));
		i = next;
		if (!pre) {
			// (blanks and line ends in HTML are one blank)
			std::string collapsed;
			bool space = false;
			for (char c : piece) {
				if (isspace((unsigned char)c)) {
					space = true;
				} else {
					if (space && !collapsed.empty())
						collapsed += ' ';
					else if (space && collapsed.empty() && !out.empty() && out.back() != '\n' && out.back() != ' ')
						collapsed += ' ';
					space = false;
					collapsed += c;
				}
			}
			if (space && !collapsed.empty())
				collapsed += ' ';
			piece = collapsed;
		}
		appendText(piece);
	}
	if (inLink && !linkText.empty())
		out += linkText;
	// the end: no blanks at the ends of lines but the two of a break, one blank line at most between paragraphs
	std::string result;
	int blank = 0;
	size_t at = 0;
	while (at <= out.size()) {
		size_t end = out.find('\n', at);
		if (end == std::string::npos)
			end = out.size();
		std::string line = out.substr(at, end - at);
		const bool hardBreak = line.size() >= 2 && line.compare(line.size() - 2, 2, "  ") == 0 && !Trimmed(line).empty();
		// (the blanks at the beginning of a line are the indent of a nested list; those at the end go, but the two of a break)
		while (!line.empty() && isspace((unsigned char)line.back()))
			line.pop_back();
		if (Trimmed(line).empty())
			line.clear();
		if (hardBreak)
			line += "  ";
		if (line.empty()) {
			blank++;
			if (blank <= 1 && !result.empty())
				result += "\n";
		} else {
			if (blank > 1 && !result.empty())
				blank = 1;
			result += line + "\n";
			blank = 0;
		}
		if (end == out.size())
			break;
		at = end + 1;
	}
	while (!result.empty() && (result.back() == '\n' || result.back() == ' '))
		result.pop_back();
	return result.empty() ? result : result + "\n";
}


std::vector<std::string>
FindUrls(const std::string& text)
{
	std::vector<std::string> urls;
	auto add = [&](const std::string& url) {
		if (url.size() > 12 && std::find(urls.begin(), urls.end(), url) == urls.end())
			urls.push_back(url);
	};
	// the targets of tags and of Markdown links come first in the text where they stand; a plain scan finds them all
	for (size_t i = 0; i < text.size(); i++) {
		if ((i == 0 || !isalnum((unsigned char)text[i - 1])) && IsHttpStart(text, i)) {
			const size_t end = UrlEnd(text, i);
			add(DecodeEntities(text.substr(i, end - i)));
			i = end > i ? end - 1 : i;
		}
	}
	return urls;
}


bool
ClassifyMeetingUrl(const std::string& url, std::string* service)
{
	size_t start = url.find("://");
	if (start == std::string::npos)
		return false;
	start += 3;
	const size_t at = url.find('@', start);
	const size_t slash = url.find_first_of("/?#", start);
	if (at != std::string::npos && (slash == std::string::npos || at < slash))
		start = at + 1;
	size_t end = url.find_first_of(":/?#", start);
	const std::string host = Lower(url.substr(start, end == std::string::npos ? std::string::npos : end - start));
	const std::string path = Lower(slash == std::string::npos ? std::string() : url.substr(slash));
	auto isHost = [&](const char* domain) {
		const std::string d = domain;
		return host == d || (host.size() > d.size() && host.compare(host.size() - d.size() - 1, d.size() + 1, "." + d) == 0);
	};
	if (isHost("zoom.us") || isHost("zoomgov.com")) {
		*service = "Zoom";
	} else if (host == "meet.google.com" || (host == "g.co" && path.compare(0, 5, "/meet") == 0)) {
		*service = "Google Meet";
	} else if (host == "teams.microsoft.com" || host == "teams.live.com" || host == "teams.microsoft.us" || isHost("teams.microsoft.com")) {
		*service = "Microsoft Teams";
	} else if (isHost("webex.com")) {
		*service = "Webex";
	} else if (host == "meet.jit.si" || isHost("8x8.vc") || host.compare(0, 6, "jitsi.") == 0 || host.find(".jitsi.") != std::string::npos) {
		*service = "Jitsi";
	} else if (isHost("gotomeeting.com") || host == "gotomeet.me" || isHost("goto.com") || host == "app.goto.com") {
		*service = "GoToMeeting";
	} else if (isHost("whereby.com")) {
		*service = "Whereby";
	} else if (host == "join.skype.com" || isHost("skype.com")) {
		*service = "Skype";
	} else {
		return false;
	}
	return true;
}


std::string
DescriptionToMarkdown(const std::string& text, const std::string& html)
{
	if (!Trimmed(html).empty())
		return HtmlToMarkdown(html);
	if (LooksLikeHtml(text))
		return HtmlToMarkdown(text);

	// plain text: the lines stay lines (two blanks make a break), a link is a link, the marks of Markdown are escaped
	std::string out;
	size_t at = 0;
	const std::string source = text;
	std::vector<std::string> lines;
	while (at <= source.size()) {
		size_t end = source.find('\n', at);
		if (end == std::string::npos)
			end = source.size();
		std::string line = source.substr(at, end - at);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.push_back(line);
		if (end == source.size())
			break;
		at = end + 1;
	}
	for (size_t n = 0; n < lines.size(); n++) {
		const std::string& line = lines[n];
		std::string rendered;
		for (size_t i = 0; i < line.size(); i++) {
			if ((i == 0 || !isalnum((unsigned char)line[i - 1])) && IsHttpStart(line, i)) {
				const size_t end = UrlEnd(line, i);
				rendered += "<" + line.substr(i, end - i) + ">";
				i = end - 1;
				continue;
			}
			const char c = line[i];
			if (c == '*' || c == '_' || c == '`' || c == '\\' || c == '<' || c == '[' || c == ']')
				rendered += '\\';
			rendered += c;
		}
		// (a line that Markdown would take for a heading, a quote or a list)
		const std::string head = Trimmed(rendered);
		if (!head.empty() && (head[0] == '#' || head[0] == '>' || ((head[0] == '-' || head[0] == '+') && head.size() > 1 && head[1] == ' ')))
			rendered = "\\" + Trimmed(rendered);
		const bool lastOfParagraph = n + 1 >= lines.size() || Trimmed(lines[n + 1]).empty() || Trimmed(line).empty();
		out += rendered + (lastOfParagraph ? "" : "  ") + "\n";
	}
	// (no blank lines at the beginning and the end, no more than one between paragraphs)
	std::string result;
	int blank = 0;
	size_t pos = 0;
	while (pos < out.size()) {
		const size_t end = out.find('\n', pos);
		const std::string line = out.substr(pos, end - pos);
		if (Trimmed(line).empty()) {
			blank++;
		} else {
			if (blank > 0 && !result.empty())
				result += "\n";
			blank = 0;
			result += line + "\n";
		}
		pos = end + 1;
	}
	return result;
}


std::string
PlainSummary(const std::string& markdown, size_t limit)
{
	std::string plain;
	for (size_t i = 0; i < markdown.size(); i++) {
		const char c = markdown[i];
		if (c == '\\' && i + 1 < markdown.size()) {
			plain += markdown[++i];
		} else if (c == '[') {
			// [text](url): the text
			const size_t close = markdown.find("](", i);
			const size_t endUrl = close == std::string::npos ? close : markdown.find(')', close);
			if (close != std::string::npos && endUrl != std::string::npos) {
				plain += markdown.substr(i + 1, close - i - 1);
				i = endUrl;
			} else {
				plain += c;
			}
		} else if (c == '<' && i + 1 < markdown.size() && IsHttpStart(markdown, i + 1)) {
			const size_t close = markdown.find('>', i);
			if (close != std::string::npos) {
				plain += markdown.substr(i + 1, close - i - 1);
				i = close;
			} else {
				plain += c;
			}
		} else if (c == '*' || c == '`' || (c == '#' && (i == 0 || markdown[i - 1] == '\n' || markdown[i - 1] == '#'))) {
			continue;
		} else if (c == '\n') {
			plain += ' ';
		} else {
			plain += c;
		}
	}
	// one line, single blanks
	std::string line;
	bool space = true;
	for (char c : plain) {
		if (isspace((unsigned char)c)) {
			if (!space)
				line += ' ';
			space = true;
		} else {
			line += c;
			space = false;
		}
	}
	line = Trimmed(line);
	if (line.size() <= limit)
		return line;
	size_t cut = limit;
	// (not in the middle of a character, and at a word if one is near)
	while (cut > 0 && ((unsigned char)line[cut] & 0xC0) == 0x80)
		cut--;
	const size_t space1 = line.rfind(' ', cut);
	if (space1 != std::string::npos && space1 > limit / 2)
		cut = space1;
	return Trimmed(line.substr(0, cut)) + "...";
}


namespace {

std::string
CleanTitle(const std::string& title)
{
	// the title: blanks become -, what a name cannot hold is left out
	std::string clean;
	bool dash = false;
	for (size_t i = 0; i < title.size(); i++) {
		const unsigned char c = (unsigned char)title[i];
		if (isspace(c) || c == '/' || c == ':' || c == '\\') {
			dash = !clean.empty();
			continue;
		}
		if (c < 0x20 || c == 0x7F || c == '"' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|')
			continue;
		if (dash) {
			clean += '-';
			dash = false;
		}
		clean += (char)c;
	}
	while (!clean.empty() && (clean.back() == '-' || clean.back() == '.'))
		clean.pop_back();
	while (!clean.empty() && (clean[0] == '-' || clean[0] == '.'))
		clean.erase(0, 1);
	if (clean.size() > 80) {
		size_t cut = 80;
		while (cut > 0 && ((unsigned char)clean[cut] & 0xC0) == 0x80)
			cut--;
		const size_t dashAt = clean.rfind('-', cut);
		if (dashAt != std::string::npos && dashAt > 40)
			cut = dashAt;
		clean = clean.substr(0, cut);
	}
	return clean;
}


/** the value of a token of a name pattern; false if it is none */
bool
TokenValue(const std::string& token, const DateTime& start, bool hasTime, const std::string& title, std::string* value)
{
	char buffer[32];
	if (token == "date")
		snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", start.year, start.month, start.day);
	else if (token == "time")
		snprintf(buffer, sizeof(buffer), "%02d%02d", start.hour, start.minute);
	else if (token == "year")
		snprintf(buffer, sizeof(buffer), "%04d", start.year);
	else if (token == "month")
		snprintf(buffer, sizeof(buffer), "%02d", start.month);
	else if (token == "day")
		snprintf(buffer, sizeof(buffer), "%02d", start.day);
	else if (token == "hour")
		snprintf(buffer, sizeof(buffer), "%02d", start.hour);
	else if (token == "minute")
		snprintf(buffer, sizeof(buffer), "%02d", start.minute);
	else if (token == "title") {
		const std::string clean = CleanTitle(title);
		*value = clean.empty() ? "Event" : clean;
		return true;
	} else
		return false;
	// (a day has no time of day)
	*value = (!hasTime && (token == "time" || token == "hour" || token == "minute")) ? std::string() : std::string(buffer);
	return true;
}


bool
IsNameSeparator(char c)
{
	return c == '_' || c == '-' || c == ' ' || c == '.';
}

}	// namespace


bool
IsValidNamePattern(const std::string& pattern, std::string* reason)
{
	auto fail = [reason](const std::string& text) {
		if (reason != nullptr)
			*reason = text;
		return false;
	};
	DateTime none;
	if (pattern.find_first_not_of(" \t") == std::string::npos)
		return fail("the pattern is empty");
	if (pattern.find('/') != std::string::npos)
		return fail("a name cannot hold a /");
	for (size_t i = 0; i < pattern.size(); i++) {
		if (pattern[i] != '{') {
			if (pattern[i] == '}')
				return fail("a } with no {");
			continue;
		}
		const size_t close = pattern.find('}', i);
		if (close == std::string::npos)
			return fail("a { with no }");
		const std::string token = pattern.substr(i + 1, close - i - 1);
		std::string value;
		if (!TokenValue(token, none, true, "", &value))
			return fail("{" + token + "} is no token: use {date} {time} {year} {month} {day} {hour} {minute} {title}");
		i = close;
	}
	return true;
}


std::string
BuildEventName(const std::string& pattern, const DateTime& start, bool hasTime, const std::string& title)
{
	const std::string& use = IsValidNamePattern(pattern) ? pattern : std::string(kDefaultNamePattern);
	std::string name;
	for (size_t i = 0; i < use.size(); i++) {
		const char c = use[i];
		if (c != '{') {
			// (what a name cannot hold)
			if ((unsigned char)c >= 0x20 && c != 0x7F && c != '"' && c != '*' && c != '?' && c != '<' && c != '>' && c != '|' && c != '\\' && c != ':' && c != '}')
				name += c;
			continue;
		}
		const size_t close = use.find('}', i);
		std::string value;
		TokenValue(use.substr(i + 1, close - i - 1), start, hasTime, title, &value);
		i = close;
		if (value.empty()) {
			// an empty token takes a separator with it: "2026-11-12__Title" is "2026-11-12_Title"
			if (i + 1 < use.size() && IsNameSeparator(use[i + 1]) && (name.empty() || name.back() == use[i + 1]))
				i++;
			continue;
		}
		name += value;
	}
	while (!name.empty() && IsNameSeparator(name.back()))
		name.pop_back();
	while (!name.empty() && IsNameSeparator(name[0]))
		name.erase(0, 1);
	if (name.size() > 200) {
		size_t cut = 200;
		while (cut > 0 && ((unsigned char)name[cut] & 0xC0) == 0x80)
			cut--;
		name = name.substr(0, cut);
	}
	return name.empty() ? "Event" : name;
}


namespace {

std::string
JoinWords(const std::vector<std::string>& words)
{
	std::string out;
	for (size_t i = 0; i < words.size(); i++) {
		if (i > 0)
			out += (i + 1 == words.size()) ? " and " : ", ";
		out += words[i];
	}
	return out;
}


std::string
OrdinalWord(int n)
{
	static const char* const names[] = {"", "first", "second", "third", "fourth", "fifth"};
	if (n >= 1 && n <= 5)
		return names[n];
	return std::to_string(n) + "th";
}


/** 1st, 2nd, 3rd, 15th */
std::string
OrdinalNumber(int n)
{
	const int last = n % 100;
	const char* suffix = "th";
	if (last < 11 || last > 13) {
		switch (n % 10) {
			case 1: suffix = "st"; break;
			case 2: suffix = "nd"; break;
			case 3: suffix = "rd"; break;
		}
	}
	return std::to_string(n) + suffix;
}

const char* const kWeekdayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kMonthNames[] = {"", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

}	// namespace


std::string
DescribeRule(const Rule& rule)
{
	if (!rule.valid || rule.frequency == Frequency::None)
		return "";
	const char* unit = "day";
	switch (rule.frequency) {
		case Frequency::Secondly: unit = "second"; break;
		case Frequency::Minutely: unit = "minute"; break;
		case Frequency::Hourly: unit = "hour"; break;
		case Frequency::Daily: unit = "day"; break;
		case Frequency::Weekly: unit = "week"; break;
		case Frequency::Monthly: unit = "month"; break;
		case Frequency::Yearly: unit = "year"; break;
		default: break;
	}
	const int interval = rule.interval > 1 ? rule.interval : 1;
	std::string text = interval == 1 ? std::string("every ") + unit : "every " + std::to_string(interval) + " " + unit + "s";

	// on which days: the weekdays (with their place in the month or year), the days of the month, the months
	if (!rule.byDay.empty()) {
		std::vector<std::string> days;
		for (const WeekDay& day : rule.byDay) {
			if (day.weekday < 0 || day.weekday > 6)
				continue;
			std::string name = kWeekdayNames[day.weekday];
			if (day.ordinal > 0)
				name = "the " + OrdinalWord(day.ordinal) + " " + name;
			else if (day.ordinal == -1)
				name = "the last " + name;
			else if (day.ordinal < -1)
				name = "the " + OrdinalWord(-day.ordinal) + " to last " + name;
			days.push_back(name);
		}
		if (!days.empty())
			text += " on " + JoinWords(days);
	}
	if (rule.frequency == Frequency::Yearly && rule.byMonth.size() == 1 && rule.byMonthDay.size() == 1 && rule.byMonthDay[0] > 0 && rule.byMonth[0] >= 1 && rule.byMonth[0] <= 12) {
		text += " on " + std::to_string(rule.byMonthDay[0]) + " " + kMonthNames[rule.byMonth[0]];
	} else {
		if (!rule.byMonthDay.empty()) {
			std::vector<std::string> days;
			bool onlyFromStart = true;
			for (int day : rule.byMonthDay) {
				onlyFromStart = onlyFromStart && day > 0;
				days.push_back(day == -1 ? std::string("last day") : (day > 0 ? OrdinalNumber(day) : OrdinalNumber(-day) + " day from the end"));
			}
			text += " on the " + JoinWords(days);
		}
		if (!rule.byMonth.empty()) {
			std::vector<std::string> months;
			for (int month : rule.byMonth) {
				if (month >= 1 && month <= 12)
					months.push_back(kMonthNames[month]);
			}
			if (!months.empty())
				text += " in " + JoinWords(months);
		}
	}

	// how long
	if (rule.count == 1)
		text += ", once";
	else if (rule.count > 1)
		text += ", " + std::to_string(rule.count) + " times";
	else if (rule.until.valid)
		text += ", until " + DateTimeText(rule.until).substr(0, 10);
	return text;
}


std::string
EventBaseName(const DateTime& start, bool hasTime, const std::string& title)
{
	return BuildEventName(kDefaultNamePattern, start, hasTime, title);
}


/** The rule that the words of the field Repeats say: the simple ones (every day, every weekday, every 2 weeks, every month, every year). False for words with more in them (on Monday, until 2030). The words are read by SenRecurrence.h of sento (which Shoji and Akita use to keep the rule beside the words), the rule by ParseRule(). */
bool
RuleFromWords(const std::string& text, Rule* rule)
{
	*rule = Rule();
	std::string rrule;
	if (!sen::recurrence::RuleFromWords(text, &rrule))
		return false;
	return ParseRule(rrule, rule) && rule->valid;
}


}	// namespace ics
