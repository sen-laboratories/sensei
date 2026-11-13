/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "PersonData.h"

namespace robin {

class FeedResolver {
public:
	/** Extracts the address of an RSS or Atom feed from the link tags of a page (`<link rel="alternate" type="application/rss+xml" href="...">`); a relative address is made whole with the address of the page. */
	static std::string DiscoverFeedFromHtml(const std::string& html, const std::string& baseUrl);

	/** Builds WebFinger URL for Fediverse handles (@user@domain or user@domain). */
	static std::string WebFingerUrl(std::string_view handle);

	/** Parses WebFinger JSON to find ActivityPub Actor link. */
	static std::string ParseActorUrlFromWebFinger(const std::string& webFingerJson);

	/** Parses ActivityPub Actor JSON to extract outbox URL, avatar, and bio. */
	static bool ParseActorJson(const std::string& actorJson, PersonProfile* profile);

	/**
	 * The public API of Mastodon for the account of a handle (`@user@host`): `https://host/api/v1/accounts/lookup?acct=user`. A server that answers an unsigned request for the actor with 401 (authorized
	 * fetch: mastodon.social does) still tells this: the name, the text, the photo and the page of the account. Empty if the handle is none.
	 */
	static std::string MastodonLookupUrl(std::string_view handle);

	/** Parses what that API says of an account into the profile (the name of the account, the text, the photo, the page and its feed `<page>.rss`). */
	static bool ParseMastodonAccountJson(const std::string& json, PersonProfile* profile);

	/** Parses a page of an ActivityPub outbox: the notes that the person wrote (not what was boosted, not replies). */
	static bool ParseOutboxJson(const std::string& outboxJson, PersonProfile* profile);

	/**
	 * The address of the first page, if the answer is the collection itself and not a page of it: the outbox of Mastodon has only a count and the address of its pages (`first`).
	 * Empty if the answer has its items, or is none.
	 */
	static std::string OutboxFirstPageUrl(const std::string& outboxJson);

	/** Builds public Bluesky author feed XRPC URL (the posts of the person, without the replies). */
	static std::string BlueskyFeedUrl(std::string_view actor);

	/** The RSS feed that Bluesky has for a profile (`https://bsky.app/profile/<handle>/rss`). */
	static std::string BlueskyRssUrl(std::string_view actor);

	/** The public XRPC address of the profile (photo, text) of a Bluesky account (a link of the profile, or a handle). */
	static std::string BlueskyProfileUrl(std::string_view actor);

	/** Parses the answer for it: the photo (`avatar`) and the text (`description`), if the profile has none yet. */
	static bool ParseBlueskyProfileJson(const std::string& json, PersonProfile* profile);

	/** Parses Bluesky getAuthorFeed JSON: the posts of the author (not the reposts), each with its address on the web. */
	static bool ParseBlueskyFeedJson(const std::string& bskyJson, PersonProfile* profile);

	/** Parses RSS 2.0 / Atom XML into recent posts (CDATA, escaped HTML and the links of Atom are understood). */
	static bool ParseRssXml(const std::string& xmlString, PersonProfile* profile);

	/** Strips HTML tags and normalizes whitespace for status snippets. */
	static std::string HtmlToPlainText(std::string_view html);

	/** Formats a FeedPost into a clean Markdown document. */
	static std::string FormatPostToMarkdown(const FeedPost& post, const std::string& authorName);

	/** The newest post is the latest status: sorts the posts of a profile by their time (those without a time keep their place after the others) and sets latestStatus and statusDate. */
	static void FinishPosts(PersonProfile* profile);
};

} // namespace robin
