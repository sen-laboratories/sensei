/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "PersonData.h"
#include "PersonSettings.h"

namespace robin {

/** How long an answer of a service about a person is kept: a profile or a photo changes rarely, what somebody posts every hour. */
const long kProfileCacheSeconds = 7L * 24 * 3600;
const long kFeedCacheSeconds = 3600;

class PersonEnricher {
public:
	/**
	 * Fetches an address. `accept` is the value of the header Accept (ActivityPub serves its JSON only to one who asks for `application/activity+json`), `maxAgeSeconds` how old an answer
	 * that was kept may be. True, with the body, if there was an answer (status 2xx).
	 */
	using HttpGetFunc = std::function<bool(const std::string& url, const char* accept, long maxAgeSeconds, std::string* body, int* httpStatus)>;

	/** @param settings which services may be asked (a service that is switched off is never asked) */
	PersonEnricher(HttpGetFunc httpGet, const Settings& settings = Settings());

	/**
	 * Runs the Robin enrichment cascade on the provided profile.
	 * Fetches avatar, bio, job title, company, sameAs links, feeds, and latest status.
	 */
	bool Enrich(PersonProfile* profile, bool forceWipe = false);

	/**
	 * Fetches the posts again: from the feed that the profile has (an RSS or Atom feed, the outbox of an ActivityPub actor, Bluesky), or from where the links of the profile lead; nothing else
	 * of the profile is looked for. The answer of the feed that was kept for less than an hour is used. True if there are posts.
	 */
	bool RefreshPosts(PersonProfile* profile);

	// Who a profile is is decided by what the file says it is, never by a name that happens to be the same: a nickname is not the name of an account on a service (the first `octocat`
	// would be somebody else's photo and job). These find the accounts that the links of the profile (`schema:sameAs`) name; empty if there is none.
	/** The user of the GitHub link of the profile. */
	static std::string GitHubUser(const PersonProfile& profile);
	/** `user@host` from a Fediverse handle (`@user@host` as nickname) or a profile link of a Fediverse server (`https://host/@user`); empty if there is none. */
	static std::string FediverseHandle(const PersonProfile& profile);
	/** The link of a profile of Bluesky (`https://bsky.app/profile/<handle>`); empty if there is none. */
	static std::string BlueskyLink(const PersonProfile& profile);

private:
	void _EnrichFromEmail(PersonProfile* profile);
	void _EnrichFromHandles(PersonProfile* profile);
	void _EnrichFromFeed(PersonProfile* profile);
	bool _ResolveActor(PersonProfile* profile, const std::string& handle);
	bool _FetchPosts(PersonProfile* profile);
	bool _FetchAvatar(PersonProfile* profile, const char* credit);
	bool _Get(const std::string& url, const char* accept, long maxAgeSeconds, std::string* body);

	HttpGetFunc fHttpGet;
	Settings fSettings;
};

} // namespace robin
