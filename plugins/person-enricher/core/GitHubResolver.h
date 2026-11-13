/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <string>
#include <string_view>

#include "PersonData.h"

namespace robin {

class GitHubResolver {
public:
	/** Extracts a GitHub username from a handle or URL (e.g. "https://github.com/octocat" -> "octocat"). */
	static std::string ExtractUsername(std::string_view input);

	/** Returns the API URL for a GitHub user. */
	static std::string UserApiUrl(const std::string& username);

	/** Returns direct PNG redirect URL for avatar (no rate limits). */
	static std::string DirectAvatarUrl(const std::string& username);

	/** Parses the GitHub user JSON response into PersonProfile. */
	static bool ParseUserJson(const std::string& jsonString, PersonProfile* profile);
};

} // namespace robin
