/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperSettings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace paperboy {

namespace {

std::string
Trim(const std::string& s)
{
	size_t begin = s.find_first_not_of(" \t\r");
	if (begin == std::string::npos)
		return "";
	return s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}

std::string
Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
	return s;
}

}	// namespace


bool
ParseBool(const std::string& value, bool fallback)
{
	std::string v = Lower(Trim(value));
	if (v == "yes" || v == "true" || v == "on" || v == "1")
		return true;
	if (v == "no" || v == "false" || v == "off" || v == "0")
		return false;
	return fallback;
}


bool
LooksLikeEmail(const std::string& text)
{
	size_t at = text.find('@');
	if (at == std::string::npos || at == 0 || text.find('@', at + 1) != std::string::npos)
		return false;
	size_t dot = text.find('.', at);
	return dot != std::string::npos && dot > at + 1 && dot + 1 < text.size()
		&& text.find_first_of(" \t") == std::string::npos;
}


void
ParseSettings(const std::string& text, Settings* settings)
{
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line)) {
		size_t hash = line.find('#');
		if (hash != std::string::npos)
			line.erase(hash);
		size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		std::string key = Lower(Trim(line.substr(0, equals)));
		std::string value = Trim(line.substr(equals + 1));

		if (key == "email") {
			if (LooksLikeEmail(value))
				settings->email = value;
		} else if (key == "unpaywall") {
			settings->unpaywall = ParseBool(value, settings->unpaywall);
		} else if (key == "rename") {
			settings->rename = ParseBool(value, settings->rename);
		} else if (key == "api_key") {
			if (value.find_first_of(" \t&?=") == std::string::npos)
				settings->apiKey = value;
		} else if (key == "rename_underscores") {
			settings->renameUnderscores = ParseBool(value, settings->renameUnderscores);
		} else if (key == "rename_pattern") {
			if (!value.empty())
				settings->renamePattern = value;
		} else if (key == "overwrite") {
			settings->overwrite = ParseBool(value, settings->overwrite);
		} else if (key == "title_search") {
			settings->titleSearch = ParseBool(value, settings->titleSearch);
		} else if (key == "local_only") {
			settings->localOnly = ParseBool(value, settings->localOnly);
		} else if (key == "max_pages") {
			int pages = std::atoi(value.c_str());
			if (pages >= 1 && pages <= 20)
				settings->maxPages = pages;
		}
	}
}

}	// namespace paperboy
