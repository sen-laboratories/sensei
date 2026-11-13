/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace robin {

/**
 * How the name that an account gives a person compares with the name that the file has: the same (up to case, blanks and punctuation), a variant (one or two letters differ: a typo, an
 * accent, "Scot" for "Scott"), or another name (a nickname, a pseudonym, somebody else). Plain C++.
 */
enum class NameMatch { Same, Variant, Different };

namespace namematch {

/** lower case, letters and digits only (the bytes of a UTF-8 letter are kept) */
inline std::string
Plain(const std::string& text)
{
	std::string plain;
	for (unsigned char c : text) {
		if (c >= 0x80)
			plain += (char) c;
		else if (c >= 'A' && c <= 'Z')
			plain += (char) (c - 'A' + 'a');
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			plain += (char) c;
	}
	return plain;
}

/** the letters of a UTF-8 text (a letter with an accent is one) */
inline std::vector<char32_t>
Letters(const std::string& text)
{
	std::vector<char32_t> letters;
	for (size_t i = 0; i < text.size();) {
		const unsigned char c = (unsigned char) text[i];
		size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
		if (i + length > text.size())
			length = 1;
		char32_t letter = length == 1 ? c : (char32_t) (c & (0xFF >> (length + 1)));
		for (size_t k = 1; k < length; k++)
			letter = (letter << 6) | ((unsigned char) text[i + k] & 0x3F);
		letters.push_back(letter);
		i += length;
	}
	return letters;
}

/** the number of letters that have to be inserted, deleted, changed or swapped to make one text the other */
inline size_t
Distance(const std::vector<char32_t>& a, const std::vector<char32_t>& b)
{
	std::vector<std::vector<size_t>> d(a.size() + 1, std::vector<size_t>(b.size() + 1, 0));
	for (size_t i = 0; i <= a.size(); i++)
		d[i][0] = i;
	for (size_t j = 0; j <= b.size(); j++)
		d[0][j] = j;
	for (size_t i = 1; i <= a.size(); i++) {
		for (size_t j = 1; j <= b.size(); j++) {
			const size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
			d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
			if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
				d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
		}
	}
	return d[a.size()][b.size()];
}

}	// namespace namematch

inline NameMatch
CompareNames(const std::string& stored, const std::string& account)
{
	const std::string a = namematch::Plain(stored), b = namematch::Plain(account);
	if (a.empty() || b.empty())
		return NameMatch::Different;
	if (a == b)
		return NameMatch::Same;
	// a short name has no variants ("Al", "Ed"); a longer one may differ by one letter, a very long one by two (a sixth of it): "Scot" for "Scott", but not "Jr" at the end
	const std::vector<char32_t> la = namematch::Letters(a), lb = namematch::Letters(b);
	const size_t length = std::min(la.size(), lb.size());
	if (length < 5)
		return NameMatch::Different;
	const size_t allowed = std::min<size_t>(2, std::max<size_t>(1, length / 6));
	return namematch::Distance(la, lb) <= allowed ? NameMatch::Variant : NameMatch::Different;
}

}	// namespace robin
