/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <string>
#include <string_view>

#include "PersonData.h"

namespace robin {

class GravatarResolver {
public:
	/** Returns normalized email: trimmed and lowercased. */
	static std::string NormalizeEmail(std::string_view email);

	/** Returns the SHA-256 hash of a normalized email address. */
	static std::string EmailHash(std::string_view email);

	/** Generates Libravatar avatar URL. */
	static std::string LibravatarAvatarUrl(const std::string& hash);

	/** Generates Gravatar avatar URL (defaults to 256px, 404 if not found). */
	static std::string GravatarAvatarUrl(const std::string& hash, int size = 256);

	/** Generates Gravatar Profile JSON URL. */
	static std::string GravatarProfileUrl(const std::string& hash);

	/** Parses a Gravatar profile JSON response (v3 or legacy v2) into PersonProfile. */
	static bool ParseProfileJson(const std::string& jsonString, PersonProfile* profile);
};

} // namespace robin
