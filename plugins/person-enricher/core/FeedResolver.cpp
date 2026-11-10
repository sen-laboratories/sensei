/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "FeedResolver.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <nlohmann/json.hpp>

#include "FeedText.h"

namespace robin {

namespace {

std::string BlueskyActor(std::string_view actor);

const size_t kMaxPosts = 20;
const size_t kTitleBytes = 60;

std::string
Lower(const std::string& text)
{
	std::string lower(text);
	for (char& c : lower)
		c = (char)std::tolower((unsigned char)c);
	return lower;
}


// An element of an XML text: found by its name from a position on; the text of its start tag (the attributes) and its content. A feed is read by looking for what is needed, not parsed: feeds
// are often not well-formed XML, and a regular expression over a long feed is a risk on its own (the library may run out of stack).
struct Element {
	bool		found = false;
	std::string	tag;			///< the start tag, `<link rel="alternate" href="..."/>`
	std::string	inner;			///< what is between the tags; empty for one that closes itself
	size_t		start = 0;		///< where its start tag is
	size_t		end = 0;		///< where the search for the next one goes on
};

// `voidElement`: an element of HTML that has no end tag (`<link ...>`), as opposed to one of XML that closes itself or has an end tag
Element
FindElement(const std::string& xml, const std::string& lowerXml, const std::string& name, size_t from = 0, bool voidElement = false)
{
	Element element;
	const std::string open = "<" + name;
	size_t at = from;
	while ((at = lowerXml.find(open, at)) != std::string::npos) {
		const size_t after = at + open.size();
		if (after >= lowerXml.size())
			break;
		const char next = lowerXml[after];
		if (!(next == '>' || next == '/' || std::isspace((unsigned char)next))) {
			at = after;
			continue;
		}
		// the end of the start tag (a `>` in a quoted value does not count)
		char quote = 0;
		size_t stop = after;
		for (; stop < xml.size(); stop++) {
			const char c = xml[stop];
			if (quote != 0) {
				if (c == quote)
					quote = 0;
			} else if (c == '"' || c == '\'') {
				quote = c;
			} else if (c == '>') {
				break;
			}
		}
		if (stop >= xml.size())
			break;
		element.found = true;
		element.start = at;
		element.tag = xml.substr(at, stop - at + 1);
		if (xml[stop - 1] == '/' || voidElement) {
			element.end = stop + 1;
			return element;
		}
		const size_t close = lowerXml.find("</" + name + ">", stop + 1);
		if (close == std::string::npos) {
			element.inner = xml.substr(stop + 1);
			element.end = xml.size();
		} else {
			element.inner = xml.substr(stop + 1, close - stop - 1);
			element.end = close + name.size() + 3;
		}
		return element;
	}
	return element;
}


// the value of an attribute in a start tag
std::string
TagAttribute(const std::string& tag, const std::string& name)
{
	const std::string lower = Lower(tag);
	size_t from = 0;
	while ((from = lower.find(name, from)) != std::string::npos) {
		const bool boundary = from > 0 && std::isspace((unsigned char)lower[from - 1]);
		size_t at = from + name.size();
		from = at;
		while (at < lower.size() && std::isspace((unsigned char)lower[at]))
			at++;
		if (!boundary || at >= lower.size() || lower[at] != '=')
			continue;
		at++;
		while (at < lower.size() && std::isspace((unsigned char)lower[at]))
			at++;
		if (at >= tag.size())
			return std::string();
		if (tag[at] == '"' || tag[at] == '\'') {
			const size_t end = tag.find(tag[at], at + 1);
			return end == std::string::npos ? std::string() : feedtext::DecodeEntities(tag.substr(at + 1, end - at - 1));
		}
		size_t end = at;
		while (end < tag.size() && !std::isspace((unsigned char)tag[end]) && tag[end] != '>' && tag[end] != '/')
			end++;
		return feedtext::DecodeEntities(tag.substr(at, end - at));
	}
	return std::string();
}


// an address that is relative to the page that it is on
std::string
ResolveUrl(const std::string& base, const std::string& href)
{
	if (href.compare(0, 7, "http://") == 0 || href.compare(0, 8, "https://") == 0)
		return href;
	const size_t scheme = base.find("://");
	if (base.empty() || scheme == std::string::npos)
		return href;
	const size_t hostEnd = base.find('/', scheme + 3);
	const std::string origin = hostEnd == std::string::npos ? base : base.substr(0, hostEnd);
	if (href.compare(0, 2, "//") == 0)
		return base.substr(0, scheme + 1) + href;
	if (!href.empty() && href[0] == '/')
		return origin + href;
	// (relative to the folder of the page)
	std::string folder = hostEnd == std::string::npos ? origin + "/" : base.substr(0, base.rfind('/') + 1);
	return folder + href;
}


// the text of a feed element as plain text, one line; CDATA and escaped HTML are understood
std::string
PlainOf(const std::string& inner)
{
	std::string text = feedtext::UnwrapCdata(inner);
	// (the HTML of a title that is written as text: `&lt;em&gt;`)
	if (text.find('<') == std::string::npos && text.find("&lt;") != std::string::npos)
		text = feedtext::DecodeEntities(text);
	return feedtext::HtmlToPlainText(text);
}


std::string
MarkdownOf(const std::string& inner)
{
	std::string text = feedtext::UnwrapCdata(inner);
	if (text.find('<') == std::string::npos && text.find("&lt;") != std::string::npos)
		text = feedtext::DecodeEntities(text);
	return feedtext::HtmlToMarkdown(text);
}


void
SetDate(FeedPost* post, const std::string& published)
{
	post->published = published;
	int64_t epoch = 0;
	if (feedtext::ParseDate(published, &epoch))
		post->epoch = epoch;
}


// the first line of a text, short, for the title and the status
std::string
TitleOf(const std::string& plain)
{
	return feedtext::TruncateUtf8(plain, kTitleBytes);
}

}	// namespace


std::string
FeedResolver::DiscoverFeedFromHtml(const std::string& html, const std::string& baseUrl)
{
	const std::string lower = Lower(html);
	size_t at = 0;
	while (true) {
		const Element link = FindElement(html, lower, "link", at, true);
		if (!link.found)
			break;
		at = link.end;
		const std::string rel = Lower(TagAttribute(link.tag, "rel"));
		const std::string type = Lower(TagAttribute(link.tag, "type"));
		if (rel.find("alternate") == std::string::npos)
			continue;
		if (type.find("rss") == std::string::npos && type.find("atom") == std::string::npos)
			continue;
		const std::string href = TagAttribute(link.tag, "href");
		if (!href.empty())
			return ResolveUrl(baseUrl, href);
	}
	return "";
}


std::string
FeedResolver::WebFingerUrl(std::string_view handle)
{
	std::string h(handle);
	if (!h.empty() && h.front() == '@')
		h.erase(0, 1);

	size_t at = h.find('@');
	if (at == std::string::npos || at == 0 || at == h.size() - 1)
		return "";

	std::string user = h.substr(0, at);
	std::string host = h.substr(at + 1);

	return "https://" + host + "/.well-known/webfinger?resource=acct:" + user + "@" + host;
}


std::string
FeedResolver::ParseActorUrlFromWebFinger(const std::string& webFingerJson)
{
	if (webFingerJson.empty())
		return "";

	try {
		auto root = nlohmann::json::parse(webFingerJson);
		if (root.contains("links") && root["links"].is_array()) {
			for (const auto& link : root["links"]) {
				if (link.contains("rel") && link["rel"] == "self" &&
				    link.contains("type") && link["type"].is_string()
				    && link["type"].get<std::string>().find("activity+json") != std::string::npos
				    && link.contains("href") && link["href"].is_string()) {
					return link["href"].get<std::string>();
				}
			}
		}
	} catch (...) {
	}
	return "";
}


bool
FeedResolver::ParseActorJson(const std::string& actorJson, PersonProfile* profile)
{
	if (profile == nullptr || actorJson.empty())
		return false;

	try {
		auto root = nlohmann::json::parse(actorJson);

		if (root.contains("name") && root["name"].is_string() && !root["name"].get<std::string>().empty()) {
			if (profile->name.empty())
				profile->name = root["name"].get<std::string>();
			else if (profile->accountName.empty())
				profile->accountName = root["name"].get<std::string>();
		}

		if (profile->bio.empty() && root.contains("summary") && root["summary"].is_string())
			profile->bio = feedtext::HtmlToPlainText(root["summary"].get<std::string>());

		if (profile->avatarUrl.empty() && root.contains("icon")) {
			const auto& icon = root["icon"];
			if (icon.is_object() && icon.contains("url") && icon["url"].is_string())
				profile->avatarUrl = icon["url"].get<std::string>();
			else if (icon.is_string())
				profile->avatarUrl = icon.get<std::string>();
		}

		if (profile->feedUrl.empty() && root.contains("outbox") && root["outbox"].is_string())
			profile->feedUrl = root["outbox"].get<std::string>();

		if (root.contains("url") && root["url"].is_string()) {
			std::string selfUrl = root["url"].get<std::string>();
			if (std::find(profile->sameAs.begin(), profile->sameAs.end(), selfUrl) == profile->sameAs.end())
				profile->sameAs.push_back(selfUrl);
		}

		return true;
	} catch (...) {
		return false;
	}
}


std::string
FeedResolver::OutboxFirstPageUrl(const std::string& outboxJson)
{
	try {
		auto root = nlohmann::json::parse(outboxJson);
		if (root.contains("orderedItems") || root.contains("items"))
			return "";
		if (root.contains("first") && root["first"].is_string())
			return root["first"].get<std::string>();
	} catch (...) {
	}
	return "";
}


bool
FeedResolver::ParseOutboxJson(const std::string& outboxJson, PersonProfile* profile)
{
	if (profile == nullptr || outboxJson.empty())
		return false;

	try {
		auto root = nlohmann::json::parse(outboxJson);
		nlohmann::json items;

		if (root.contains("orderedItems") && root["orderedItems"].is_array())
			items = root["orderedItems"];
		else if (root.contains("items") && root["items"].is_array())
			items = root["items"];
		else if (root.contains("first") && root["first"].is_object() && root["first"].contains("orderedItems"))
			items = root["first"]["orderedItems"];

		for (const auto& item : items) {
			if (!item.is_object())
				continue;
			// what somebody else wrote and was boosted is not what this person says
			if (item.contains("type") && item["type"] == "Announce")
				continue;
			nlohmann::json object = item;
			if (item.contains("type") && item["type"] == "Create" && item.contains("object")) {
				if (!item["object"].is_object())
					continue;
				object = item["object"];
			}
			if (!object.contains("content") || !object["content"].is_string())
				continue;
			// a reply is half of a conversation: not a status
			if (object.contains("inReplyTo") && !object["inReplyTo"].is_null())
				continue;

			FeedPost post;
			if (object.contains("published") && object["published"].is_string())
				SetDate(&post, object["published"].get<std::string>());
			if (object.contains("url") && object["url"].is_string())
				post.url = object["url"].get<std::string>();
			else if (object.contains("id") && object["id"].is_string())
				post.url = object["id"].get<std::string>();

			const std::string raw = object["content"].get<std::string>();
			post.contentMarkdown = feedtext::HtmlToMarkdown(raw);
			post.title = TitleOf(feedtext::HtmlToPlainText(raw));

			if (object.contains("attachment") && object["attachment"].is_array() && !object["attachment"].empty()) {
				const auto& attachment = object["attachment"][0];
				if (attachment.is_object() && attachment.contains("url") && attachment["url"].is_string())
					post.mediaUrl = attachment["url"].get<std::string>();
			}

			if (!post.title.empty() && profile->recentPosts.size() < kMaxPosts)
				profile->recentPosts.push_back(post);
		}
		FinishPosts(profile);
		return !profile->recentPosts.empty();
	} catch (...) {
		return false;
	}
}


std::string
FeedResolver::BlueskyFeedUrl(std::string_view actor)
{
	return "https://public.api.bsky.app/xrpc/app.bsky.feed.getAuthorFeed?actor=" + BlueskyActor(actor) + "&limit=20&filter=posts_no_replies";
}


namespace {

// the handle or DID of a link of a profile of Bluesky (`https://bsky.app/profile/alice.bsky.social/post/3k44` is `alice.bsky.social`), or of a handle
std::string
BlueskyActor(std::string_view actor)
{
	std::string a(actor);
	const std::string bskyPrefix = "bsky.app/profile/";
	size_t pos = a.find(bskyPrefix);
	if (pos != std::string::npos) {
		a = a.substr(pos + bskyPrefix.length());
		size_t end = a.find_first_of("/?#");
		if (end != std::string::npos)
			a = a.substr(0, end);
	}
	return a;
}

}	// namespace


std::string
FeedResolver::BlueskyRssUrl(std::string_view actor)
{
	return "https://bsky.app/profile/" + BlueskyActor(actor) + "/rss";
}


std::string
FeedResolver::BlueskyProfileUrl(std::string_view actor)
{
	return "https://public.api.bsky.app/xrpc/app.bsky.actor.getProfile?actor=" + BlueskyActor(actor);
}


bool
FeedResolver::ParseBlueskyProfileJson(const std::string& json, PersonProfile* profile)
{
	if (profile == nullptr || json.empty())
		return false;
	try {
		auto root = nlohmann::json::parse(json);
		if (!root.is_object())
			return false;
		if (profile->avatarUrl.empty() && root.contains("avatar") && root["avatar"].is_string())
			profile->avatarUrl = root["avatar"].get<std::string>();
		if (profile->bio.empty() && root.contains("description") && root["description"].is_string())
			profile->bio = root["description"].get<std::string>();
		return true;
	} catch (...) {
		return false;
	}
}


bool
FeedResolver::ParseBlueskyFeedJson(const std::string& bskyJson, PersonProfile* profile)
{
	if (profile == nullptr || bskyJson.empty())
		return false;

	try {
		auto root = nlohmann::json::parse(bskyJson);
		if (!root.contains("feed") || !root["feed"].is_array())
			return false;

		for (const auto& item : root["feed"]) {
			if (!item.is_object() || !item.contains("post") || !item["post"].is_object())
				continue;
			// a repost is somebody else's post
			if (item.contains("reason") && item["reason"].is_object())
				continue;

			const auto& postObj = item["post"];
			FeedPost post;

			// Bluesky names a post by an AT URI (at://did:plc:.../app.bsky.feed.post/3k44...), which no browser opens: the address on the web is made of the handle and the key
			std::string handle;
			if (postObj.contains("author") && postObj["author"].is_object() && postObj["author"].contains("handle") && postObj["author"]["handle"].is_string())
				handle = postObj["author"]["handle"].get<std::string>();
			if (postObj.contains("uri") && postObj["uri"].is_string())
				post.url = feedtext::WebUrl(postObj["uri"].get<std::string>(), handle);

			if (postObj.contains("record") && postObj["record"].is_object()) {
				const auto& rec = postObj["record"];
				if (rec.contains("text") && rec["text"].is_string()) {
					post.contentMarkdown = rec["text"].get<std::string>();
					post.title = TitleOf(feedtext::HtmlToPlainText(post.contentMarkdown));
				}
				if (rec.contains("createdAt") && rec["createdAt"].is_string())
					SetDate(&post, rec["createdAt"].get<std::string>());
			}

			if (postObj.contains("embed") && postObj["embed"].is_object()) {
				const auto& embed = postObj["embed"];
				if (embed.contains("images") && embed["images"].is_array() && !embed["images"].empty()) {
					if (embed["images"][0].contains("fullsize") && embed["images"][0]["fullsize"].is_string())
						post.mediaUrl = embed["images"][0]["fullsize"].get<std::string>();
				}
			}

			if (!post.title.empty() && profile->recentPosts.size() < kMaxPosts)
				profile->recentPosts.push_back(post);
		}
		FinishPosts(profile);
		return !profile->recentPosts.empty();
	} catch (...) {
		return false;
	}
}


bool
FeedResolver::ParseRssXml(const std::string& xmlString, PersonProfile* profile)
{
	if (profile == nullptr || xmlString.empty())
		return false;

	const std::string lower = Lower(xmlString);
	size_t at = 0;
	while (profile->recentPosts.size() < kMaxPosts) {
		// the next <item> (RSS) or <entry> (Atom), whichever comes first
		const Element item = FindElement(xmlString, lower, "item", at);
		const Element entry = FindElement(xmlString, lower, "entry", at);
		const Element* block = item.found && (!entry.found || item.start <= entry.start) ? &item : entry.found ? &entry : NULL;
		if (block == NULL)
			break;
		at = block->end;

		const std::string& inner = block->inner;
		const std::string innerLower = Lower(inner);
		FeedPost post;

		const Element title = FindElement(inner, innerLower, "title");
		if (title.found)
			post.title = PlainOf(title.inner);

		// the address: the text of <link> (RSS), or the href of the link that is the alternate of the entry (Atom; there may be others: `rel="self"`, `rel="enclosure"`)
		size_t from = 0;
		while (true) {
			const Element link = FindElement(inner, innerLower, "link", from);
			if (!link.found)
				break;
			from = link.end;
			const std::string href = TagAttribute(link.tag, "href");
			const std::string rel = Lower(TagAttribute(link.tag, "rel"));
			if (!href.empty() && (rel.empty() || rel == "alternate")) {
				post.url = href;
				break;
			}
			const std::string text = feedtext::UnwrapCdata(link.inner);
			if (href.empty() && !text.empty()) {
				post.url = text;
				while (!post.url.empty() && std::isspace((unsigned char)post.url.back()))
					post.url.pop_back();
				while (!post.url.empty() && std::isspace((unsigned char)post.url.front()))
					post.url.erase(0, 1);
				break;
			}
		}

		for (const char* name : {"pubdate", "published", "updated", "dc:date"}) {
			const Element date = FindElement(inner, innerLower, name);
			if (date.found) {
				std::string text = feedtext::UnwrapCdata(date.inner);
				while (!text.empty() && std::isspace((unsigned char)text.back()))
					text.pop_back();
				SetDate(&post, text);
				break;
			}
		}

		for (const char* name : {"content:encoded", "content", "description", "summary"}) {
			const Element content = FindElement(inner, innerLower, name);
			if (content.found && !content.inner.empty()) {
				post.contentMarkdown = MarkdownOf(content.inner);
				break;
			}
		}

		const Element media = FindElement(inner, innerLower, "enclosure");
		if (media.found && Lower(TagAttribute(media.tag, "type")).compare(0, 6, "image/") == 0)
			post.mediaUrl = TagAttribute(media.tag, "url");

		if (post.title.empty() && !post.contentMarkdown.empty())
			post.title = TitleOf(feedtext::HtmlToPlainText(post.contentMarkdown));

		if (!post.title.empty())
			profile->recentPosts.push_back(post);
	}
	FinishPosts(profile);
	return !profile->recentPosts.empty();
}


void
FeedResolver::FinishPosts(PersonProfile* profile)
{
	if (profile == nullptr)
		return;
	// the newest first (the posts that have a time; the others stay where they were, after them)
	std::stable_sort(profile->recentPosts.begin(), profile->recentPosts.end(), [](const FeedPost& a, const FeedPost& b) {
		if ((a.epoch != 0) != (b.epoch != 0))
			return a.epoch != 0;
		return a.epoch > b.epoch;
	});
	if (profile->recentPosts.size() > kMaxPosts)
		profile->recentPosts.resize(kMaxPosts);
	if (!profile->recentPosts.empty()) {
		profile->latestStatus = profile->recentPosts.front().title;
		profile->statusDate = profile->recentPosts.front().published;
		profile->statusEpoch = profile->recentPosts.front().epoch;
	}
}


std::string
FeedResolver::HtmlToPlainText(std::string_view html)
{
	return feedtext::HtmlToPlainText(html);
}


std::string
FeedResolver::FormatPostToMarkdown(const FeedPost& post, const std::string& authorName)
{
	std::ostringstream ss;
	ss << "# " << (post.title.empty() ? "Status Update" : post.title) << "\n\n";

	if (!authorName.empty() || post.epoch != 0) {
		ss << "*By " << (authorName.empty() ? "Author" : authorName);
		if (post.epoch != 0)
			ss << " on " << feedtext::FormatIso(post.epoch);
		else if (!post.published.empty())
			ss << " on " << post.published;
		ss << "*\n\n";
	}

	ss << (post.contentMarkdown.empty() ? post.title : post.contentMarkdown) << "\n\n";

	if (!post.mediaUrl.empty())
		ss << "![" << feedtext::HtmlToPlainText(post.title) << "](" << post.mediaUrl << ")\n\n";

	// (an AT URI is turned into the address of the post on the web when the post is read; if one is left it is no link)
	const std::string url = feedtext::WebUrl(post.url);
	if (!url.empty()) {
		ss << "---\n";
		ss << "[Original Post](" << url << ")\n";
	}
	return ss.str();
}

} // namespace robin
