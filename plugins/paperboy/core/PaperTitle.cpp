/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperTitle.h"

#include <cctype>
#include <set>
#include <sstream>

namespace paperboy {

std::string
NormalizeTitle(const std::string& title)
{
	std::string out;
	bool blank = true;
	for (unsigned char c : title) {
		// bytes of UTF-8 sequences (>= 0x80) are letters of other scripts and stay
		if (std::isalnum(c) || c >= 0x80) {
			out += (char) std::tolower(c);
			blank = false;
		} else if (!blank) {
			out += ' ';
			blank = true;
		}
	}
	if (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}


double
TitleSimilarity(const std::string& a, const std::string& b)
{
	auto words = [](const std::string& title) {
		std::set<std::string> set;
		std::istringstream stream(NormalizeTitle(title));
		std::string word;
		while (stream >> word)
			set.insert(word);
		return set;
	};
	std::set<std::string> first = words(a), second = words(b);
	if (first.empty() || second.empty())
		return 0;

	size_t common = 0;
	for (const std::string& word : first)
		common += second.count(word);
	return (double) common / (double) (first.size() + second.size() - common);
}


bool
LooksLikeTitle(const std::string& title)
{
	std::string t = NormalizeTitle(title);
	if (t.size() < 12 || t.find(' ') == std::string::npos)
		return false;

	size_t words = 1;
	for (char c : t)
		words += (c == ' ');
	if (words < 3)
		return false;

	static const char* const kJunkPrefixes[] = { "microsoft word", "microsoft powerpoint", "untitled", "slide ", "document " };
	for (const char* junk : kJunkPrefixes) {
		if (t.compare(0, std::string(junk).size(), junk) == 0)
			return false;
	}

	static const char* const kJunkSuffixes[] = { " pdf", " doc", " docx", " tex", " dvi", " ps", " indd", " qxd", " pmd", " fm" };
	for (const char* suffix : kJunkSuffixes) {
		std::string s = suffix;
		if (t.size() > s.size() && t.compare(t.size() - s.size(), s.size(), s) == 0)
			return false;
	}
	return true;
}

}	// namespace paperboy
