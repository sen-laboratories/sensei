/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "GravatarResolver.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "Sha256.h"

namespace robin {

std::string
GravatarResolver::NormalizeEmail(std::string_view email)
{
	// Trim leading/trailing whitespace
	size_t start = 0;
	while (start < email.size() && std::isspace(static_cast<unsigned char>(email[start])))
		start++;

	size_t end = email.size();
	while (end > start && std::isspace(static_cast<unsigned char>(email[end - 1])))
		end--;

	std::string normalized;
	normalized.reserve(end - start);
	for (size_t i = start; i < end; i++) {
		normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(email[i]))));
	}
	return normalized;
}


std::string
GravatarResolver::EmailHash(std::string_view email)
{
	return Sha256::Compute(NormalizeEmail(email));
}


std::string
GravatarResolver::LibravatarAvatarUrl(const std::string& hash)
{
	return "https://seccdn.libravatar.org/avatar/" + hash + "?d=404&s=256";
}


std::string
GravatarResolver::GravatarAvatarUrl(const std::string& hash, int size)
{
	return "https://gravatar.com/avatar/" + hash + "?d=404&s=" + std::to_string(size);
}


std::string
GravatarResolver::GravatarProfileUrl(const std::string& hash)
{
	// Gravatar v3 profiles / fallback json
	return "https://api.gravatar.com/v3/profiles/" + hash;
}


bool
GravatarResolver::ParseProfileJson(const std::string& jsonString, PersonProfile* profile)
{
	if (profile == nullptr || jsonString.empty())
		return false;

	try {
		auto root = nlohmann::json::parse(jsonString);

		// Handle Gravatar v3 or v2 (which nests in "entry" array)
		nlohmann::json data = root;
		if (root.contains("entry") && root["entry"].is_array() && !root["entry"].empty()) {
			data = root["entry"][0];
		}

		std::string accountName;
		if (data.contains("display_name") && data["display_name"].is_string())
			accountName = data["display_name"].get<std::string>();
		else if (data.contains("displayName") && data["displayName"].is_string())
			accountName = data["displayName"].get<std::string>();
		if (!accountName.empty()) {
			if (profile->name.empty())
				profile->name = accountName;
			else if (profile->accountName.empty())
				profile->accountName = accountName;
		}

		// the photo of the profile (v3 `avatar_url`, v2 `thumbnailUrl`): for a profile that was found by its name the photo is not a hash of the e-mail address away
		if (profile->avatarUrl.empty()) {
			if (data.contains("avatar_url") && data["avatar_url"].is_string())
				profile->avatarUrl = data["avatar_url"].get<std::string>();
			else if (data.contains("thumbnailUrl") && data["thumbnailUrl"].is_string())
				profile->avatarUrl = data["thumbnailUrl"].get<std::string>();
		}

		if (data.contains("job_title") && data["job_title"].is_string() && profile->jobTitle.empty())
			profile->jobTitle = data["job_title"].get<std::string>();
		else if (data.contains("jobTitle") && data["jobTitle"].is_string() && profile->jobTitle.empty())
			profile->jobTitle = data["jobTitle"].get<std::string>();

		if (data.contains("company") && data["company"].is_string() && profile->company.empty())
			profile->company = data["company"].get<std::string>();

		if (data.contains("current_location") && data["current_location"].is_string() && profile->location.empty())
			profile->location = data["current_location"].get<std::string>();
		else if (data.contains("currentLocation") && data["currentLocation"].is_string() && profile->location.empty())
			profile->location = data["currentLocation"].get<std::string>();

		if (data.contains("description") && data["description"].is_string() && profile->bio.empty())
			profile->bio = data["description"].get<std::string>();
		else if (data.contains("aboutMe") && data["aboutMe"].is_string() && profile->bio.empty())
			profile->bio = data["aboutMe"].get<std::string>();

		// Verified accounts / profile URLs
		if (data.contains("verified_accounts") && data["verified_accounts"].is_array()) {
			for (const auto& account : data["verified_accounts"]) {
				if (account.contains("url") && account["url"].is_string()) {
					std::string u = account["url"].get<std::string>();
					if (std::find(profile->sameAs.begin(), profile->sameAs.end(), u) == profile->sameAs.end())
						profile->sameAs.push_back(u);
				}
			}
		}

		if (data.contains("urls") && data["urls"].is_array()) {
			for (const auto& item : data["urls"]) {
				if (item.contains("value") && item["value"].is_string()) {
					std::string u = item["value"].get<std::string>();
					if (profile->homepage.empty())
						profile->homepage = u;
					else if (std::find(profile->sameAs.begin(), profile->sameAs.end(), u) == profile->sameAs.end())
						profile->sameAs.push_back(u);
				}
			}
		}

		return true;
	} catch (...) {
		return false;
	}
}

} // namespace robin
