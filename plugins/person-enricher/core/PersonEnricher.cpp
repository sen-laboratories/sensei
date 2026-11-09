/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PersonEnricher.h"

#include <algorithm>

#include "FeedResolver.h"
#include "GitHubResolver.h"
#include "GravatarResolver.h"

namespace robin {

namespace {

const char* const kAcceptJson = "application/json";
const char* const kAcceptActivity = "application/activity+json, application/ld+json;q=0.9, application/json;q=0.8";
const char* const kAcceptWebFinger = "application/jrd+json, application/json;q=0.9";
const char* const kAcceptFeed = "application/activity+json, application/rss+xml;q=0.9, application/atom+xml;q=0.9, application/xml;q=0.8, text/xml;q=0.8, */*;q=0.5";
const char* const kAcceptHtml = "text/html, application/xhtml+xml;q=0.9, */*;q=0.5";
const char* const kAcceptImage = "image/*, */*;q=0.5";

// servers whose profile links look like those of a Fediverse server (`/@name`) and are none
bool
IsNoFediverseHost(const std::string& host)
{
	static const char* hosts[] = {"medium.com", "tiktok.com", "youtube.com", "twitter.com", "x.com", "instagram.com", "patreon.com", "bsky.app", "substack.com", "facebook.com"};
	for (const char* known : hosts) {
		const std::string name = known;
		if (host == name || (host.size() > name.size() && host.compare(host.size() - name.size() - 1, std::string::npos, "." + name) == 0))
			return true;
	}
	return false;
}

bool
StartsWith(const std::string& text, char c)
{
	return !text.empty() && text.front() == c;
}

}	// namespace


PersonEnricher::PersonEnricher(HttpGetFunc httpGet, const Settings& settings)
	:
	fHttpGet(std::move(httpGet)),
	fSettings(settings)
{
}


std::string
PersonEnricher::GitHubUser(const PersonProfile& profile)
{
	for (const auto& link : profile.sameAs) {
		if (link.find("github.com/") == std::string::npos)
			continue;
		const std::string user = GitHubResolver::ExtractUsername(link);
		// (the page of a person, not of an organization, a topic or a search: `github.com/orgs/x`, `github.com/search`)
		if (!user.empty() && user != "orgs" && user != "search" && user != "topics" && user != "sponsors" && user != "features")
			return user;
	}
	return "";
}


std::string
PersonEnricher::FediverseHandle(const PersonProfile& profile)
{
	// `@user@host` as a nickname: a handle is written so; `user@host` could be an e-mail address
	if (StartsWith(profile.nick, '@') && profile.nick.find('@', 1) != std::string::npos)
		return profile.nick.substr(1);

	for (const auto& link : profile.sameAs) {
		const size_t at = link.find("/@");
		const size_t hostStart = link.find("://");
		if (at == std::string::npos || hostStart == std::string::npos || at < hostStart)
			continue;
		const std::string host = link.substr(hostStart + 3, at - hostStart - 3);
		if (host.empty() || host.find('/') != std::string::npos || IsNoFediverseHost(host))
			continue;
		std::string user = link.substr(at + 2);
		const size_t end = user.find_first_of("/?#");
		if (end != std::string::npos)
			user = user.substr(0, end);
		if (!user.empty() && user.find('@') == std::string::npos)
			return user + "@" + host;
	}
	return "";
}


std::string
PersonEnricher::BlueskyLink(const PersonProfile& profile)
{
	for (const auto& link : profile.sameAs) {
		if (link.find("bsky.app/profile/") != std::string::npos)
			return link;
	}
	return "";
}


bool
PersonEnricher::_Get(const std::string& url, const char* accept, long maxAgeSeconds, std::string* body)
{
	int status = 0;
	body->clear();
	return fHttpGet(url, accept, maxAgeSeconds, body, &status) && !body->empty() && (status == 0 || (status >= 200 && status < 300));
}


bool
PersonEnricher::Enrich(PersonProfile* profile, bool forceWipe)
{
	if (profile == nullptr)
		return false;

	if (forceWipe) {
		profile->avatarData.clear();
		profile->avatarUrl.clear();
		profile->latestStatus.clear();
		profile->statusDate.clear();
		profile->statusEpoch = 0;
		profile->recentPosts.clear();
	}

	// 1. Email-based enrichment (Libravatar -> Gravatar)
	if (!profile->mbox.empty() && fSettings.emailServices)
		_EnrichFromEmail(profile);

	// 2. The accounts that the profile links to (GitHub, Fediverse, Bluesky)
	_EnrichFromHandles(profile);

	// 3. Feed discovery & the posts
	_EnrichFromFeed(profile);

	return profile->HasAvatar() || profile->HasProfileData();
}


bool
PersonEnricher::RefreshPosts(PersonProfile* profile)
{
	if (profile == nullptr)
		return false;
	profile->recentPosts.clear();
	profile->latestStatus.clear();
	profile->statusDate.clear();
	profile->statusEpoch = 0;

	// a person whose feed is not known yet, but whose Fediverse account is
	if (profile->feedUrl.empty() && fSettings.fediverse) {
		const std::string handle = FediverseHandle(*profile);
		if (!handle.empty())
			_ResolveActor(profile, handle);
	}
	_EnrichFromFeed(profile);
	return !profile->recentPosts.empty();
}


void
PersonEnricher::_EnrichFromEmail(PersonProfile* profile)
{
	std::string hash = GravatarResolver::EmailHash(profile->mbox);

	// A. Try Libravatar / Gravatar avatar image (not if the photo is not wanted: nothing is downloaded that is thrown away)
	if (profile->avatarData.empty() && fSettings.photo) {
		std::string libravatarUrl = GravatarResolver::LibravatarAvatarUrl(hash);
		std::string imgData;
		if (_Get(libravatarUrl, kAcceptImage, kProfileCacheSeconds, &imgData)) {
			profile->avatarData = std::move(imgData);
			profile->avatarUrl = libravatarUrl;
			profile->avatarCredit = "Libravatar";
		} else {
			// Fallback to Gravatar avatar
			std::string gravatarUrl = GravatarResolver::GravatarAvatarUrl(hash, 256);
			if (_Get(gravatarUrl, kAcceptImage, kProfileCacheSeconds, &imgData)) {
				profile->avatarData = std::move(imgData);
				profile->avatarUrl = gravatarUrl;
				profile->avatarCredit = "Gravatar";
			}
		}
	}

	// B. Try Gravatar Profile JSON for bio, job title, company, accounts
	std::string jsonBody;
	if (_Get(GravatarResolver::GravatarProfileUrl(hash), kAcceptJson, kProfileCacheSeconds, &jsonBody))
		GravatarResolver::ParseProfileJson(jsonBody, profile);
}


bool
PersonEnricher::_FetchAvatar(PersonProfile* profile, const char* credit)
{
	if (!fSettings.photo || !profile->avatarData.empty() || profile->avatarUrl.empty())
		return false;
	std::string imgData;
	if (!_Get(profile->avatarUrl, kAcceptImage, kProfileCacheSeconds, &imgData))
		return false;
	profile->avatarData = std::move(imgData);
	profile->avatarCredit = credit;
	return true;
}


bool
PersonEnricher::_ResolveActor(PersonProfile* profile, const std::string& handle)
{
	const std::string webFingerUrl = FeedResolver::WebFingerUrl(handle);
	std::string webFinger;
	if (webFingerUrl.empty() || !_Get(webFingerUrl, kAcceptWebFinger, kProfileCacheSeconds, &webFinger))
		return false;
	const std::string actorUrl = FeedResolver::ParseActorUrlFromWebFinger(webFinger);
	std::string actor;
	if (actorUrl.empty() || !_Get(actorUrl, kAcceptActivity, kProfileCacheSeconds, &actor))
		return false;
	return FeedResolver::ParseActorJson(actor, profile);
}


void
PersonEnricher::_EnrichFromHandles(PersonProfile* profile)
{
	// GitHub: the account that a link of the profile names (not a nickname that is the same as somebody's account)
	const std::string ghUser = fSettings.github ? GitHubUser(*profile) : std::string();
	if (!ghUser.empty()) {
		std::string ghJson;
		if (_Get(GitHubResolver::UserApiUrl(ghUser), kAcceptJson, kProfileCacheSeconds, &ghJson))
			GitHubResolver::ParseUserJson(ghJson, profile);

		// If still no avatar, fetch GitHub avatar directly
		if (profile->avatarData.empty() && fSettings.photo) {
			std::string imgData;
			const std::string ghAvatarUrl = GitHubResolver::DirectAvatarUrl(ghUser);
			if (_Get(ghAvatarUrl, kAcceptImage, kProfileCacheSeconds, &imgData)) {
				profile->avatarData = std::move(imgData);
				profile->avatarUrl = ghAvatarUrl;
				profile->avatarCredit = "GitHub";
			}
		}
	}

	// Fediverse (ActivityPub): the actor of the handle, with its photo, its text and its outbox
	const std::string fediHandle = fSettings.fediverse ? FediverseHandle(*profile) : std::string();
	if (!fediHandle.empty() && _ResolveActor(profile, fediHandle))
		_FetchAvatar(profile, "Fediverse");

	// Bluesky: the profile (photo, text) and, for the posts, the address of its feed
	const std::string bsky = fSettings.bluesky ? BlueskyLink(*profile) : std::string();
	if (!bsky.empty()) {
		const std::string actor = FeedResolver::BlueskyRssUrl(bsky);		// (https://bsky.app/profile/<handle>/rss)
		std::string profileJson;
		if (_Get(FeedResolver::BlueskyProfileUrl(bsky), kAcceptJson, kProfileCacheSeconds, &profileJson))
			FeedResolver::ParseBlueskyProfileJson(profileJson, profile);
		_FetchAvatar(profile, "Bluesky");
		if (profile->feedUrl.empty())
			profile->feedUrl = actor;
	}
}


bool
PersonEnricher::_FetchPosts(PersonProfile* profile)
{
	// Bluesky has its own, richer answer than the feed of RSS (images, no reposts); the feed of RSS is the one that is kept for the person
	const std::string bsky = fSettings.bluesky ? BlueskyLink(*profile) : std::string();
	std::string body;
	if (!bsky.empty() && _Get(FeedResolver::BlueskyFeedUrl(bsky), kAcceptJson, kFeedCacheSeconds, &body) && FeedResolver::ParseBlueskyFeedJson(body, profile))
		return true;

	if (!fSettings.feeds || profile->feedUrl.empty() || !_Get(profile->feedUrl, kAcceptFeed, kFeedCacheSeconds, &body))
		return false;

	size_t first = body.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
	if (first != std::string::npos && body[first] == '{') {
		// ActivityPub: the outbox is the collection, and its items are on its pages
		const std::string page = FeedResolver::OutboxFirstPageUrl(body);
		if (!page.empty()) {
			std::string pageBody;
			if (!_Get(page, kAcceptActivity, kFeedCacheSeconds, &pageBody))
				return false;
			return FeedResolver::ParseOutboxJson(pageBody, profile);
		}
		return FeedResolver::ParseOutboxJson(body, profile);
	}
	return FeedResolver::ParseRssXml(body, profile);
}


void
PersonEnricher::_EnrichFromFeed(PersonProfile* profile)
{
	// If no feed is known yet, but homepage is known, discover RSS/Atom
	if (fSettings.feeds && profile->feedUrl.empty() && !profile->homepage.empty()) {
		std::string htmlBody;
		if (_Get(profile->homepage, kAcceptHtml, kProfileCacheSeconds, &htmlBody)) {
			const std::string discovered = FeedResolver::DiscoverFeedFromHtml(htmlBody, profile->homepage);
			if (!discovered.empty())
				profile->feedUrl = discovered;
		}
	}

	if (profile->recentPosts.empty())
		_FetchPosts(profile);
}

} // namespace robin
