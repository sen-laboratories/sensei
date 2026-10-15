/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperDoi.h"

#include <algorithm>
#include <cctype>

namespace paperboy {

namespace {

bool
IsDigit(char c)
{
	return c >= '0' && c <= '9';
}

bool
IsAlnum(char c)
{
	return IsDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/** characters that a DOI may contain (the pattern of the DOI Handbook) */
bool
IsDoiChar(char c)
{
	return IsAlnum(c) || c == '-' || c == '.' || c == '_' || c == ';' || c == '(' || c == ')' || c == '/' || c == ':';
}

std::string
Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
	return s;
}

bool
StartsWithNoCase(const std::string& s, size_t pos, const char* prefix)
{
	for (size_t i = 0; prefix[i] != '\0'; i++) {
		if (pos + i >= s.size() || std::tolower((unsigned char) s[pos + i]) != prefix[i])
			return false;
	}
	return true;
}

/** punctuation that ends a sentence, not the DOI; a closing parenthesis only if it has no opening one */
std::string
TrimTrailing(std::string doi)
{
	for (;;) {
		if (doi.empty())
			return doi;
		char last = doi.back();
		if (last == '.' || last == ',' || last == ';' || last == ':' || last == '-' || last == '_') {
			doi.pop_back();
		} else if (last == ')') {
			if (std::count(doi.begin(), doi.end(), '(') >= std::count(doi.begin(), doi.end(), ')'))
				return doi;
			doi.pop_back();
		} else {
			return doi;
		}
	}
}

/** "10.NNNN/suffix" with a registrant code of 4 to 9 digits and a suffix of at least 2 characters */
bool
IsDoiShape(const std::string& doi)
{
	if (doi.size() < 10 || doi.compare(0, 3, "10.") != 0)
		return false;
	size_t i = 3;
	while (i < doi.size() && IsDigit(doi[i]))
		i++;
	size_t digits = i - 3;
	if (digits < 4 || digits > 9 || i >= doi.size() || doi[i] != '/')
		return false;
	return doi.size() - (i + 1) >= 2;
}

}	// namespace


std::string
NormalizeDoi(const std::string& raw)
{
	size_t begin = 0, end = raw.size();
	while (begin < end && std::isspace((unsigned char) raw[begin]))
		begin++;
	while (end > begin && std::isspace((unsigned char) raw[end - 1]))
		end--;
	std::string s = raw.substr(begin, end - begin);

	static const char* const kPrefixes[] = {
		"https://doi.org/", "http://doi.org/", "https://dx.doi.org/", "http://dx.doi.org/", "doi.org/", "dx.doi.org/",
		"urn:doi:", "doi:", "doi"
	};
	for (const char* prefix : kPrefixes) {
		if (StartsWithNoCase(s, 0, prefix)) {
			s.erase(0, std::string(prefix).size());
			while (!s.empty() && (std::isspace((unsigned char) s.front()) || s.front() == ':'))
				s.erase(s.begin());
			break;
		}
	}

	// stop at the first character that cannot be part of a DOI
	size_t stop = 0;
	while (stop < s.size() && IsDoiChar(s[stop]))
		stop++;
	s = TrimTrailing(Lower(s.substr(0, stop)));
	return IsDoiShape(s) ? s : "";
}


std::vector<std::string>
FindDois(const std::string& text)
{
	std::vector<std::string> found;
	size_t pos = 0;
	while ((pos = text.find("10.", pos)) != std::string::npos) {
		size_t start = pos;
		pos += 3;

		// "10." must start a token: after a blank, a colon, a slash or "doi" ("doi10.1016/..."), not inside a number
		if (start > 0) {
			char before = text[start - 1];
			bool ok = !IsAlnum(before) && before != '.' && before != '-';
			if (!ok && start >= 3 && StartsWithNoCase(text, start - 3, "doi"))
				ok = true;
			if (!ok)
				continue;
		}

		size_t end = start;
		while (end < text.size() && IsDoiChar(text[end]))
			end++;
		std::string doi = NormalizeDoi(text.substr(start, end - start));
		if (doi.empty())
			continue;
		if (std::find(found.begin(), found.end(), doi) == found.end())
			found.push_back(doi);
		pos = end;
	}
	return found;
}


std::vector<std::string>
FindArxivIds(const std::string& text)
{
	std::vector<std::string> found;
	static const char* const kMarkers[] = { "arxiv:", "arxiv.org/abs/", "arxiv.org/pdf/" };

	// one scan over the text, so that the ids come in the order of the text
	for (size_t pos = 0; pos < text.size(); pos++) {
		size_t length = 0;
		for (const char* marker : kMarkers) {
			if (StartsWithNoCase(text, pos, marker)) {
				length = std::string(marker).size();
				break;
			}
		}
		if (length == 0)
			continue;

		size_t i = pos + length;
		while (i < text.size() && text[i] == ' ')
			i++;
		size_t start = i;
		std::string id;

		// new style: YYMM.NNNN or YYMM.NNNNN
		size_t digits = 0;
		while (i < text.size() && IsDigit(text[i]) && digits < 4) { i++; digits++; }
		if (digits == 4 && i < text.size() && text[i] == '.') {
			size_t number = 0;
			i++;
			while (i < text.size() && IsDigit(text[i]) && number < 5) { i++; number++; }
			if (number == 4 || number == 5)
				id = text.substr(start, i - start);
		} else {
			// old style: archive[.XX]/YYMMNNN
			i = start;
			while (i < text.size() && (std::islower((unsigned char) text[i]) || text[i] == '-'))
				i++;
			if (i > start) {
				if (i + 2 < text.size() && text[i] == '.' && std::isupper((unsigned char) text[i + 1])
						&& std::isupper((unsigned char) text[i + 2]))
					i += 3;
				if (i < text.size() && text[i] == '/') {
					size_t number = 0, first = ++i;
					while (i < text.size() && IsDigit(text[i]) && number < 7) { i++; number++; }
					if (number == 7)
						id = Lower(text.substr(start, first + number - start));
				}
			}
		}
		if (!id.empty() && std::find(found.begin(), found.end(), id) == found.end())
			found.push_back(id);
	}
	return found;
}


std::string
ArxivDoi(const std::string& arxivId)
{
	return arxivId.empty() ? "" : "10.48550/arxiv." + Lower(arxivId);
}

}	// namespace paperboy
