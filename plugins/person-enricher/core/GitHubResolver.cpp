/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "GitHubResolver.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace robin {

std::string
GitHubResolver::ExtractUsername(std::string_view input)
{
	std::string s(input);
	// Trim leading @
	if (!s.empty() && s.front() == '@')
		s.erase(0, 1);

	// If it's a URL like https://github.com/username
	const std::string prefix = "github.com/";
	size_t pos = s.find(prefix);
	if (pos != std::string::npos) {
		s = s.substr(pos + prefix.length());
		size_t slash = s.find('/');
		if (slash != std::string::npos)
			s = s.substr(0, slash);
		size_t q = s.find('?');
		if (q != std::string::npos)
			s = s.substr(0, q);
	}

	// Trim whitespace
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
	s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());

	return s;
}


std::string
GitHubResolver::UserApiUrl(const std::string& username)
{
	return "https://api.github.com/users/" + username;
}


std::string
GitHubResolver::DirectAvatarUrl(const std::string& username)
{
	return "https://github.com/" + username + ".png";
}


bool
GitHubResolver::ParseUserJson(const std::string& jsonString, PersonProfile* profile)
{
	if (profile == nullptr || jsonString.empty())
		return false;

	try {
		auto root = nlohmann::json::parse(jsonString);

		if (profile->name.empty() && root.contains("name") && root["name"].is_string())
			profile->name = root["name"].get<std::string>();

		if (profile->company.empty() && root.contains("company") && root["company"].is_string())
			profile->company = root["company"].get<std::string>();

		if (profile->location.empty() && root.contains("location") && root["location"].is_string())
			profile->location = root["location"].get<std::string>();

		if (profile->bio.empty() && root.contains("bio") && root["bio"].is_string())
			profile->bio = root["bio"].get<std::string>();

		if (profile->homepage.empty() && root.contains("blog") && root["blog"].is_string()) {
			std::string blog = root["blog"].get<std::string>();
			if (!blog.empty()) {
				if (blog.find("http://") != 0 && blog.find("https://") != 0)
					blog = "https://" + blog;
				profile->homepage = blog;
			}
		}

		if (root.contains("html_url") && root["html_url"].is_string()) {
			std::string ghUrl = root["html_url"].get<std::string>();
			if (std::find(profile->sameAs.begin(), profile->sameAs.end(), ghUrl) == profile->sameAs.end())
				profile->sameAs.push_back(ghUrl);
		}

		if (profile->avatarUrl.empty() && root.contains("avatar_url") && root["avatar_url"].is_string())
			profile->avatarUrl = root["avatar_url"].get<std::string>();

		return true;
	} catch (...) {
		return false;
	}
}

} // namespace robin
