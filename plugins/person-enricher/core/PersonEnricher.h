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

	/** How good a photo is (the size of its longer side in pixels; a program that can read images says it; without one the number of bytes): the better one is kept when several services have a photo. */
	using PhotoJudge = std::function<float(const std::string& imageData)>;

	/** @param settings which services may be asked (a service that is switched off is never asked) */
	PersonEnricher(HttpGetFunc httpGet, const Settings& settings = Settings());

	void SetPhotoJudge(PhotoJudge judge) { fJudge = std::move(judge); }

	/**
	 * Runs the Robin enrichment on the provided profile: every service that is on, in the order of the list of the settings, is asked for the account that the profile names (a link, a handle, an
	 * e-mail address, a homepage). What an earlier service gave is not replaced by a later one (what the profile had before is never replaced); the photo is the exception, the best one is kept.
	 * Fetches avatar, bio, job title, company, sameAs links, feeds, and latest status.
	 */
	bool Enrich(PersonProfile* profile, bool forceWipe = false);

	/** Whether the profile names an account at a service of the list (`github`, `mastodon`, `bluesky`, `wikidata`, `orcid`): the search by name is for the others. */
	static bool HasAccount(const PersonProfile& profile, const std::string& service);

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
	/** The link of a profile of Bluesky (`https://bsky.app/profile/<handle>`), or a handle of Bluesky (`@name.bsky.social`) as nickname; empty if there is none. */
	static std::string BlueskyLink(const PersonProfile& profile);
	/** The links that say who a person is, wherever the person has them: `schema:sameAs` and the homepage (the URL field of a contact of the People application is the only place that it has). */
	static std::vector<std::string> Links(const PersonProfile& profile);
	/** What a link of Gravatar names: the hash of an avatar (`gravatar.com/avatar/<hash>`) or the name of a profile (`gravatar.com/<name>`), for the profile of the service; empty if there is none. */
	static std::string GravatarId(const PersonProfile& profile);
	/** What the enrichment can go on, as the names of the sources (`Gravatar for the e-mail address`, `GitHub (alice)`...): empty for a person that has no e-mail address and no link of a known service. */
	static std::vector<std::string> Sources(const PersonProfile& profile, const Settings& settings = Settings());
	/** What the enrichment had to go on, in a sentence for the user who is told that nothing was found: the sources that it looked at, or what the person lacks so that there is nothing to look at. */
	static std::string Explain(const PersonProfile& profile, const Settings& settings = Settings());

private:
	void _EnrichFromEmail(PersonProfile* profile);
	/** the profile of Gravatar for an identifier: the hash of an e-mail address (the photo too, from Libravatar or Gravatar) or the name of a profile (the photo is the one that the profile gives) */
	void _EnrichFromGravatar(PersonProfile* profile, const std::string& id, bool isHash);
	void _EnrichFromGitHub(PersonProfile* profile);
	void _EnrichFromFediverse(PersonProfile* profile);
	void _EnrichFromBluesky(PersonProfile* profile);
	/** the feed of the homepage, if no service gave a feed */
	void _DiscoverFeed(PersonProfile* profile);
	/** the posts, from the first service of the list that has them */
	void _EnrichFromFeed(PersonProfile* profile);
	/** runs a step that may find a photo, and keeps the better of its photo and the one that the profile had */
	void _WithBestPhoto(PersonProfile* profile, const std::function<void()>& step);
	bool _FetchFeedPosts(PersonProfile* profile);
	bool _ResolveActor(PersonProfile* profile, const std::string& handle);
	bool _FetchPosts(PersonProfile* profile);
	bool _FetchAvatar(PersonProfile* profile, const char* credit);
	bool _Get(const std::string& url, const char* accept, long maxAgeSeconds, std::string* body);

	HttpGetFunc fHttpGet;
	Settings fSettings;
	PhotoJudge fJudge;
};

} // namespace robin
