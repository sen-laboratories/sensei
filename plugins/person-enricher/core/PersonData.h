/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace robin {

struct FeedPost {
	std::string title;				///< the first words, one line
	std::string contentMarkdown;	///< the post as Markdown
	std::string published;			///< as the feed wrote it
	int64_t		epoch = 0;			///< `published` as seconds since 1970 (UTC); 0 if the feed gave no date that could be read
	std::string url;				///< the address of the post on the web (never an AT URI: see feedtext::WebUrl())
	std::string mediaUrl;
};

struct PersonProfile {
	std::string name;
	std::string givenName;
	std::string familyName;
	std::string nick;
	std::string mbox;
	std::string phone;
	std::string homepage;
	std::string birthDate;
	std::string jobTitle;
	std::string company;
	std::string bio;
	std::string location;
	std::string orcid;				///< the ORCID iD of a researcher
	std::vector<std::string> sameAs;

	// Photo & Media
	std::string avatarUrl;
	std::string avatarData;
	std::string avatarLicense;
	std::string avatarCredit;

	// Microfeed & Status
	std::string feedUrl;
	std::string latestStatus;
	std::string statusDate;			///< as the feed wrote it
	int64_t		statusEpoch = 0;	///< when the latest post was made (seconds since 1970); 0 if unknown
	std::vector<FeedPost> recentPosts;

	bool HasAvatar() const { return !avatarData.empty(); }
	bool HasProfileData() const {
		return !jobTitle.empty() || !bio.empty() || !location.empty() || !company.empty() || !orcid.empty() ||
		       !homepage.empty() || !sameAs.empty() || !latestStatus.empty();
	}
};

} // namespace robin
