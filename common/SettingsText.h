/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file SettingsText.h
 * @brief The settings files of plugins: lines of `key = value` (`#` starts a comment). Plain C++, no Haiku API (tested on every platform).
 *
 * A plugin reads its file with its own parser (which settings exist, what a valid value is) and writes a change with Update(): the line of a key is replaced, a comment after the value
 * stays, a key that has no line is added at the end, and everything else (comments, unknown lines, the order) is kept, so a file that was edited by hand is not rewritten.
 */

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace sensei {
namespace settingstext {

inline std::string
Trim(const std::string& s)
{
	const size_t begin = s.find_first_not_of(" \t\r");
	if (begin == std::string::npos)
		return "";
	return s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}


inline std::string
Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}


/** true for "yes", "true", "on" and "1" (any case), false for "no", "false", "off", "0"; else @p fallback. */
inline bool
ParseBool(const std::string& value, bool fallback)
{
	const std::string v = Lower(Trim(value));
	if (v == "yes" || v == "true" || v == "on" || v == "1")
		return true;
	if (v == "no" || v == "false" || v == "off" || v == "0")
		return false;
	return fallback;
}


/** Whether the text is a boolean in the way that ParseBool() reads one. */
inline bool
IsBool(const std::string& value)
{
	return ParseBool(value, true) == ParseBool(value, false);
}


/** The key/value pairs of a text, in order; the key in lower case, the value trimmed, comments and lines without `=` left out. */
inline std::vector<std::pair<std::string, std::string>>
Pairs(const std::string& text)
{
	std::vector<std::pair<std::string, std::string>> pairs;
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line)) {
		const size_t hash = line.find('#');
		if (hash != std::string::npos)
			line.erase(hash);
		const size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		pairs.push_back({Lower(Trim(line.substr(0, equals))), Trim(line.substr(equals + 1))});
	}
	return pairs;
}


/** The text of a settings file with the changed values put in (see the top of this file). @p changes are key and value pairs. */
inline std::string
Update(const std::string& text, const std::vector<std::pair<std::string, std::string>>& changes)
{
	std::vector<std::string> lines;
	{
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
			lines.push_back(line);
	}

	for (const auto& change : changes) {
		bool found = false;
		for (std::string& line : lines) {
			const size_t hash = line.find('#');
			const size_t equals = line.find('=');
			if (equals == std::string::npos || (hash != std::string::npos && hash < equals))
				continue;
			if (Lower(Trim(line.substr(0, equals))) != change.first)
				continue;
			// the key as it was written, the new value, the comment after the old one
			const std::string comment = hash != std::string::npos ? " " + line.substr(hash) : std::string();
			line = Trim(line.substr(0, equals)) + " = " + change.second + comment;
			found = true;
			break;
		}
		if (!found)
			lines.push_back(change.first + " = " + change.second);
	}

	std::string result;
	for (const std::string& line : lines)
		result += line + "\n";
	return result;
}

}	// namespace settingstext
}	// namespace sensei
