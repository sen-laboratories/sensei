/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PersonEnricher.h"

#include <algorithm>
#include <iterator>

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
	for (const auto& link : Links(profile)) {
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

	for (const auto& link : Links(profile)) {
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
	for (const auto& link : Links(profile)) {
		if (link.find("bsky.app/profile/") != std::string::npos)
			return link;
	}
	// a handle of Bluesky as nickname: `@name.bsky.social` (a handle on a domain of one's own cannot be told from a name, it is a link then)
	std::string nick = profile.nick;
	if (StartsWith(nick, '@'))
		nick = nick.substr(1);
	const std::string suffix = ".bsky.social";
	if (nick.size() > suffix.size() && nick.compare(nick.size() - suffix.size(), suffix.size(), suffix) == 0 && nick.find('@') == std::string::npos
			&& nick.find_first_of(" /:") == std::string::npos)
		return "https://bsky.app/profile/" + nick;
	return "";
}


std::vector<std::string>
PersonEnricher::Links(const PersonProfile& profile)
{
	std::vector<std::string> links = profile.sameAs;
	if (!profile.homepage.empty() && std::find(links.begin(), links.end(), profile.homepage) == links.end())
		links.push_back(profile.homepage);
	return links;
}


std::string
PersonEnricher::GravatarId(const PersonProfile& profile)
{
	for (const auto& link : Links(profile)) {
		const size_t scheme = link.find("://");
		if (scheme == std::string::npos)
			continue;
		const size_t hostEnd = link.find('/', scheme + 3);
		if (hostEnd == std::string::npos)
			continue;
		const std::string host = link.substr(scheme + 3, hostEnd - scheme - 3);
		const bool isGravatar = host == "gravatar.com" || (host.size() > 13 && host.compare(host.size() - 13, 13, ".gravatar.com") == 0);
		if (!isGravatar)
			continue;
		std::string path = link.substr(hostEnd + 1);
		path = path.substr(0, path.find_first_of("?#"));
		const size_t slash = path.find('/');
		const std::string first = path.substr(0, slash);
		std::string second = slash == std::string::npos ? std::string() : path.substr(slash + 1);
		second = second.substr(0, second.find('/'));
		auto strip = [](std::string name) {
			for (const char* extension : {".json", ".xml", ".png", ".jpg", ".jpeg"}) {
				const std::string ext = extension;
				if (name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
					return name.substr(0, name.size() - ext.size());
			}
			return name;
		};
		if (first == "avatar") {
			const std::string hash = strip(second);
			if ((hash.size() == 32 || hash.size() == 64) && hash.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
				return hash;
			continue;
		}
		static const char* notProfiles[] = {"profiles", "js", "embed", "site", "connect", "emails", "about", "docs", "avatar", "profile", "login", "signup", "support"};
		const std::string slug = strip(first);
		if (slug.empty() || std::find(std::begin(notProfiles), std::end(notProfiles), slug) != std::end(notProfiles))
			continue;
		return slug;
	}
	return "";
}


std::vector<std::string>
PersonEnricher::Sources(const PersonProfile& profile, const Settings& settings)
{
	std::vector<std::string> looked;
	if (!profile.mbox.empty() && settings.emailServices)
		looked.push_back("Gravatar for the e-mail address");
	if (!GravatarId(profile).empty() && settings.emailServices)
		looked.push_back("the Gravatar profile");
	if (!GitHubUser(profile).empty() && settings.github)
		looked.push_back("GitHub (" + GitHubUser(profile) + ")");
	if (!FediverseHandle(profile).empty() && settings.fediverse)
		looked.push_back("the Fediverse (" + FediverseHandle(profile) + ")");
	if (!BlueskyLink(profile).empty() && settings.bluesky)
		looked.push_back("Bluesky");
	if (!profile.homepage.empty() && settings.feeds)
		looked.push_back("the homepage");
	return looked;
}


std::string
PersonEnricher::Explain(const PersonProfile& profile, const Settings& settings)
{
	const std::vector<std::string> looked = Sources(profile, settings);
	if (looked.empty()) {
		return "There is nothing to look up. Add an e-mail address or a link to a profile (GitHub, Mastodon, Bluesky, Gravatar) or a homepage to the person; "
			"a service that is switched off in the settings of Robin is not asked.";
	}
	std::string text = "Looked at ";
	for (size_t i = 0; i < looked.size(); i++)
		text += (i == 0 ? "" : (i + 1 == looked.size() ? " and " : ", ")) + looked[i];
	return text + ": nothing public was found there.";
}


bool
PersonEnricher::_Get(const std::string& url, const char* accept, long maxAgeSeconds, std::string* body)
{
	int status = 0;
	body->clear();
	return fHttpGet(url, accept, maxAgeSeconds, body, &status) && !body->empty() && (status == 0 || (status >= 200 && status < 300));
}


bool
PersonEnricher::HasAccount(const PersonProfile& profile, const std::string& service)
{
	if (service == "github")
		return !GitHubUser(profile).empty();
	if (service == "mastodon")
		return !FediverseHandle(profile).empty();
	if (service == "bluesky")
		return !BlueskyLink(profile).empty();
	if (service == "orcid") {
		if (!profile.orcid.empty())
			return true;
		for (const std::string& link : Links(profile)) {
			if (link.find("orcid.org/") != std::string::npos)
				return true;
		}
		return false;
	}
	if (service == "wikidata") {
		for (const std::string& link : Links(profile)) {
			if (link.find("wikidata.org/wiki/Q") != std::string::npos || link.find("wikidata.org/entity/Q") != std::string::npos)
				return true;
		}
	}
	return false;
}


void
PersonEnricher::_WithBestPhoto(PersonProfile* profile, const std::function<void()>& step)
{
	const std::string data = std::move(profile->avatarData), url = profile->avatarUrl, credit = profile->avatarCredit, license = profile->avatarLicense;
	profile->avatarData.clear();
	profile->avatarUrl.clear();
	profile->avatarCredit.clear();
	profile->avatarLicense.clear();

	step();

	auto restore = [&]() {
		profile->avatarData = data;
		profile->avatarUrl = url;
		profile->avatarCredit = credit;
		profile->avatarLicense = license;
	};
	if (data.empty()) {
		// (nothing before: what the step found stays; if it found no photo at all, the address that the profile had stays)
		if (profile->avatarData.empty() && profile->avatarUrl.empty())
			restore();
		return;
	}
	const bool better = !profile->avatarData.empty() && (fJudge ? fJudge(profile->avatarData) > fJudge(data) : profile->avatarData.size() > data.size());
	if (!better)
		restore();
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

	// every service that is on, in the order of the list: what an earlier one gave is not replaced (each step fills what is empty), the photo is the best of all
	for (const std::string& id : fSettings.ServiceOrder()) {
		if (!fSettings.Uses(id))
			continue;
		if (id == "email") {
			_WithBestPhoto(profile, [this, profile]() {
				// Libravatar -> Gravatar by the e-mail address; or, without one, a link to a Gravatar profile or avatar
				if (!profile->mbox.empty()) {
					_EnrichFromEmail(profile);
				} else {
					const std::string gravatar = GravatarId(*profile);
					if (!gravatar.empty())
						_EnrichFromGravatar(profile, gravatar, gravatar.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos && (gravatar.size() == 32 || gravatar.size() == 64));
				}
			});
		} else if (id == "github") {
			_WithBestPhoto(profile, [this, profile]() { _EnrichFromGitHub(profile); });
		} else if (id == "mastodon") {
			_WithBestPhoto(profile, [this, profile]() { _EnrichFromFediverse(profile); });
		} else if (id == "bluesky") {
			_WithBestPhoto(profile, [this, profile]() { _EnrichFromBluesky(profile); });
		} else if (id == "feeds") {
			_DiscoverFeed(profile);
		}
	}

	// the posts, from the first service of the list that has them
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
	if (profile->feedUrl.empty() && fSettings.Uses("mastodon")) {
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


void
PersonEnricher::_EnrichFromGravatar(PersonProfile* profile, const std::string& id, bool isHash)
{
	if (isHash && profile->avatarData.empty() && fSettings.photo) {
		std::string imgData;
		const std::string gravatarUrl = GravatarResolver::GravatarAvatarUrl(id, 256);
		if (_Get(gravatarUrl, kAcceptImage, kProfileCacheSeconds, &imgData)) {
			profile->avatarData = std::move(imgData);
			profile->avatarUrl = gravatarUrl;
			profile->avatarCredit = "Gravatar";
		}
	}
	std::string jsonBody;
	if (_Get(GravatarResolver::GravatarProfileUrl(id), kAcceptJson, kProfileCacheSeconds, &jsonBody))
		GravatarResolver::ParseProfileJson(jsonBody, profile);
	// (the photo that the profile names: for a profile by its name there is no hash of the e-mail address)
	if (profile->avatarData.empty() && !profile->avatarUrl.empty())
		_FetchAvatar(profile, "Gravatar");
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
	if (!actorUrl.empty() && _Get(actorUrl, kAcceptActivity, kProfileCacheSeconds, &actor) && FeedResolver::ParseActorJson(actor, profile))
		return true;
	// a server with authorized fetch (401 for an actor that is asked for without a signature): the public API of Mastodon tells the account, too
	const std::string lookupUrl = FeedResolver::MastodonLookupUrl(handle);
	std::string account;
	return !lookupUrl.empty() && _Get(lookupUrl, kAcceptJson, kProfileCacheSeconds, &account) && FeedResolver::ParseMastodonAccountJson(account, profile);
}


void
PersonEnricher::_EnrichFromGitHub(PersonProfile* profile)
{
	// the account that a link of the profile names (not a nickname that is the same as somebody's account)
	const std::string ghUser = GitHubUser(*profile);
	if (ghUser.empty())
		return;
	std::string ghJson;
	if (_Get(GitHubResolver::UserApiUrl(ghUser), kAcceptJson, kProfileCacheSeconds, &ghJson))
		GitHubResolver::ParseUserJson(ghJson, profile);

	// the photo of the account
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


void
PersonEnricher::_EnrichFromFediverse(PersonProfile* profile)
{
	// Fediverse (ActivityPub): the actor of the handle, with its photo, its text and its outbox
	const std::string fediHandle = FediverseHandle(*profile);
	if (fediHandle.empty())
		return;
	const bool hadFeed = !profile->feedUrl.empty();
	if (_ResolveActor(profile, fediHandle))
		_FetchAvatar(profile, "Fediverse");
	if (!hadFeed && !profile->feedUrl.empty())
		profile->feedService = "mastodon";
}


void
PersonEnricher::_EnrichFromBluesky(PersonProfile* profile)
{
	// the profile (photo, text) and, for the posts, the address of its feed
	const std::string bsky = BlueskyLink(*profile);
	if (bsky.empty())
		return;
	const std::string actor = FeedResolver::BlueskyRssUrl(bsky);		// (https://bsky.app/profile/<handle>/rss)
	std::string profileJson;
	if (_Get(FeedResolver::BlueskyProfileUrl(bsky), kAcceptJson, kProfileCacheSeconds, &profileJson))
		FeedResolver::ParseBlueskyProfileJson(profileJson, profile);
	_FetchAvatar(profile, "Bluesky");
	if (profile->feedUrl.empty()) {
		profile->feedUrl = actor;
		profile->feedService = "bluesky";
	}
}


bool
PersonEnricher::_FetchPosts(PersonProfile* profile)
{
	// the first service of the list that has posts: Bluesky has its own, richer answer than the feed of RSS (images, no reposts); the feed that is kept for the person is the one of the Fediverse or
	// of the homepage
	bool triedFeed = false;
	for (const std::string& id : fSettings.ServiceOrder()) {
		if (!fSettings.Uses(id))
			continue;
		std::string body;
		if (id == "bluesky") {
			const std::string bsky = BlueskyLink(*profile);
			if (!bsky.empty() && _Get(FeedResolver::BlueskyFeedUrl(bsky), kAcceptJson, kFeedCacheSeconds, &body) && FeedResolver::ParseBlueskyFeedJson(body, profile))
				return true;
		} else if ((id == "mastodon" || id == "feeds") && !triedFeed) {
			triedFeed = true;
			if (_FetchFeedPosts(profile))
				return true;
		}
	}
	return false;
}


bool
PersonEnricher::_FetchFeedPosts(PersonProfile* profile)
{
	if (profile->feedUrl.empty())
		return false;
	// (the feed belongs to the service that gave it; one from the file, or from a homepage, is that of the homepages)
	const std::string owner = profile->feedService.empty() ? "feeds" : profile->feedService;
	if (!fSettings.Uses(owner))
		return false;
	std::string body;
	if (!_Get(profile->feedUrl, kAcceptFeed, kFeedCacheSeconds, &body))
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
PersonEnricher::_DiscoverFeed(PersonProfile* profile)
{
	// If no feed is known yet, but a homepage is, discover RSS/Atom
	if (profile->feedUrl.empty() && !profile->homepage.empty()) {
		std::string htmlBody;
		if (_Get(profile->homepage, kAcceptHtml, kProfileCacheSeconds, &htmlBody)) {
			const std::string discovered = FeedResolver::DiscoverFeedFromHtml(htmlBody, profile->homepage);
			if (!discovered.empty()) {
				profile->feedUrl = discovered;
				profile->feedService = "feeds";
			}
		}
	}
}


void
PersonEnricher::_EnrichFromFeed(PersonProfile* profile)
{
	if (profile->recentPosts.empty())
		_FetchPosts(profile);
}

} // namespace robin
