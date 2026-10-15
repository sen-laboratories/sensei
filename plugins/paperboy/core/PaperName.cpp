/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperName.h"

#include <cctype>

namespace paperboy {

namespace {

/** a byte that continues a UTF-8 sequence (10xxxxxx) */
bool
IsContinuation(unsigned char c)
{
	return (c & 0xC0) == 0x80;
}

void
ReplaceAll(std::string* text, const std::string& what, const std::string& with)
{
	for (size_t pos = text->find(what); pos != std::string::npos; pos = text->find(what, pos + with.size()))
		text->replace(pos, what.size(), with);
}

}	// namespace


std::string
FamilyName(const std::string& displayName)
{
	std::string name = displayName;
	size_t comma = name.find(',');
	if (comma != std::string::npos)
		return name.substr(0, comma);

	size_t end = name.find_last_not_of(" \t");
	if (end == std::string::npos)
		return "";
	name.erase(end + 1);
	size_t blank = name.find_last_of(" \t");
	return blank == std::string::npos ? name : name.substr(blank + 1);
}


std::string
SanitizeForFileName(const std::string& text, size_t maxBytes, bool underscores)
{
	const std::string blank = underscores ? "_" : " ";
	std::string out;
	bool lastBlank = true;		// also drops leading blanks
	auto put = [&](const std::string& piece, bool isBlank) {
		if (isBlank) {
			if (!lastBlank)
				out += blank;
			lastBlank = true;
		} else {
			out += piece;
			lastBlank = false;
		}
	};

	for (unsigned char c : text) {
		if (!std::isspace(c) && (c < 0x20 || c == 0x7F || std::string("\\*?\"<>|").find((char) c) != std::string::npos))
			continue;
		if (c == '/') {
			put("-", false);
		} else if (c == ':') {
			if (underscores) {
				put("", true);
			} else {
				put("", true);
				put("-", false);
				put("", true);
			}
		} else if (std::isspace(c) || (underscores && c == '_')) {
			put("", true);
		} else {
			put(std::string(1, (char) c), false);
		}
	}

	if (out.size() > maxBytes) {
		size_t cut = maxBytes;
		// not inside a UTF-8 sequence, and preferably at the end of a word
		while (cut > 0 && IsContinuation((unsigned char) out[cut]))
			cut--;
		size_t word = out.rfind(blank, cut);
		if (word != std::string::npos && word > maxBytes / 2)
			cut = word;
		out.erase(cut);
	}
	auto trailing = [](char c) { return c == '_' || c == '.' || c == '-' || c == ' '; };
	while (!out.empty() && trailing(out.back()))
		out.pop_back();
	while (!out.empty() && (out.front() == '.' || out.front() == '-' || out.front() == ' ' || out.front() == '_'))
		out.erase(out.begin());
	return out;
}


std::string
BuildFileName(const std::string& pattern, const Paper& paper, const std::string& extension, bool underscores)
{
	if (paper.title.empty())
		return "";

	std::string year = paper.year > 0 ? std::to_string(paper.year) : paper.date.substr(0, 4);
	std::string author = paper.authors.empty() ? "" : SanitizeForFileName(FamilyName(paper.authors.front().name), 40, underscores);

	std::string doi = paper.doi;
	ReplaceAll(&doi, "/", "_");

	std::string name = pattern.empty() ? kDefaultNamePattern : pattern;
	ReplaceAll(&name, "{year}", year);
	ReplaceAll(&name, "{first_author}", author);
	ReplaceAll(&name, "{title}", SanitizeForFileName(paper.title, 100, underscores));
	ReplaceAll(&name, "{doi}", SanitizeForFileName(doi, 80, true));

	// an empty token leaves a gap: "2015__Deep learning" -> "2015_Deep learning"
	for (size_t pos = name.find("__"); pos != std::string::npos; pos = name.find("__"))
		name.erase(pos, 1);
	while (!name.empty() && name.front() == '_')
		name.erase(name.begin());
	while (!name.empty() && name.back() == '_')
		name.pop_back();
	name = SanitizeForFileName(name, 200, underscores);
	return name.empty() ? "" : name + extension;
}

}	// namespace paperboy
