/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "MovieName.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <vector>

namespace movie {

namespace {

std::string
Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}


bool
IsYear(const std::string& token)
{
	if (token.size() != 4 || !std::isdigit((unsigned char)token[0]))
		return false;
	for (char c : token) {
		if (!std::isdigit((unsigned char)c))
			return false;
	}
	const int year = std::stoi(token);
	return year >= 1888 && year <= 2100;		// (the first film, and as far as a person can tell)
}


/** the words that say how a file was made, not what the movie is called */
bool
IsReleaseTag(const std::string& token)
{
	static const std::set<std::string> tags = {
		"480p", "576p", "720p", "1080p", "1080i", "2160p", "4k", "uhd", "hdr", "hdr10", "bluray", "blu-ray", "bdrip", "brrip", "bdremux", "dvdrip", "dvdscr", "dvd", "webrip", "web-dl",
		"webdl", "web", "hdtv", "hdrip", "hdcam", "cam", "x264", "x265", "h264", "h265", "hevc", "xvid", "divx", "aac", "ac3", "dts", "dd5", "remux", "proper", "repack",
		"extended", "unrated", "uncut", "remastered", "imax", "multi", "dual", "10bit", "8bit", "internal", "limited", "criterion", "directors", "theatrical",
	};
	return tags.count(Lower(token)) > 0;
}


bool
IsVideoExtension(const std::string& extension)
{
	static const std::set<std::string> extensions = {
		"mkv", "mp4", "m4v", "avi", "mov", "wmv", "flv", "webm", "mpg", "mpeg", "ts", "m2ts", "mts", "ogv", "3gp", "divx", "vob", "iso", "rmvb",
	};
	return extensions.count(Lower(extension)) > 0;
}

}	// namespace


Guess
GuessFromFileName(const std::string& fileName)
{
	std::string name = fileName;
	const size_t dot = name.rfind('.');
	if (dot != std::string::npos && IsVideoExtension(name.substr(dot + 1)))
		name.erase(dot);

	// the brackets: a year in them is the year, the rest ([YTS.MX], (Remastered)) is not the title
	int bracketYear = 0;
	std::string cleaned;
	for (size_t i = 0; i < name.size();) {
		const char open = name[i];
		const char close = open == '(' ? ')' : open == '[' ? ']' : open == '{' ? '}' : 0;
		const size_t end = close != 0 ? name.find(close, i + 1) : std::string::npos;
		if (end == std::string::npos) {
			cleaned += open;
			i++;
			continue;
		}
		const std::string inside = name.substr(i + 1, end - i - 1);
		if (IsYear(inside))
			bracketYear = std::stoi(inside);
		cleaned += ' ';
		i = end + 1;
	}

	for (char& c : cleaned) {
		if (c == '.' || c == '_')
			c = ' ';
	}

	std::vector<std::string> tokens;
	std::string current;
	for (char c : cleaned + " ") {
		if (c == ' ' || c == '\t') {
			if (!current.empty())
				tokens.push_back(current);
			current.clear();
		} else {
			current += c;
		}
	}

	// the title ends at the year (not the first word: a number there is part of the title), and at the first word of how the file was made
	size_t end = tokens.size();
	int year = bracketYear;
	for (size_t i = 0; i < tokens.size(); i++) {
		if (IsReleaseTag(tokens[i])) {
			end = std::min(end, i);
			break;
		}
	}
	if (bracketYear == 0) {
		for (size_t i = end; i > 1; i--) {
			if (IsYear(tokens[i - 1])) {
				year = std::stoi(tokens[i - 1]);
				end = i - 1;
				break;
			}
		}
	}

	Guess guess;
	for (size_t i = 0; i < end; i++) {
		if (i > 0)
			guess.title += ' ';
		guess.title += tokens[i];
	}
	// (what is left of a name in the style of "Title - Group")
	while (!guess.title.empty() && (guess.title.back() == '-' || guess.title.back() == ' '))
		guess.title.pop_back();
	guess.year = year;
	return guess;
}

}	// namespace movie
