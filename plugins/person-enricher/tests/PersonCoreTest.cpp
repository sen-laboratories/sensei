/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "Sha256.h"
#include "GravatarResolver.h"
#include "Hunt.h"
#include "GitHubResolver.h"
#include "FeedResolver.h"
#include "FeedText.h"
#include "PersonSettings.h"
#include "../../../common/BoxScale.h"
#include "PersonEnricher.h"

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

namespace {

// a person's services, as scripted answers: the address (as asked, the Accept header with it) to the body
struct FakeWeb {
	std::map<std::string, std::string>	answers;
	std::vector<std::string>			asked;
	std::map<std::string, std::string>	accepts;

	robin::PersonEnricher::HttpGetFunc Get()
	{
		return [this](const std::string& url, const char* accept, long, std::string* body, int* status) {
			asked.push_back(url);
			accepts[url] = accept != NULL ? accept : "";
			auto found = answers.find(url);
			if (found == answers.end()) {
				*status = 404;
				return false;
			}
			*body = found->second;
			*status = 200;
			return true;
		};
	}

	bool Asked(const std::string& part) const
	{
		for (const std::string& url : asked) {
			if (url.find(part) != std::string::npos)
				return true;
		}
		return false;
	}
};

}	// namespace

int
main()
{
	std::printf("Running Robin Core Unit Tests...\n");

	// 1. SHA-256 (the values are those of `shasum -a 256`)
	{
		CHECK(robin::Sha256::Compute("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
		CHECK(robin::Sha256::Compute("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
		CHECK(robin::Sha256::Compute("hello") == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
		CHECK(robin::Sha256::Compute("Robin - Enriching People") == "6ee69108933643b7090a395541d9503e080a1f8a3c1de30a3a625fa5ac585225");
		// (more than one block of 64 bytes)
		CHECK(robin::Sha256::Compute("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	}

	// 2. Gravatar normalization and URL building
	{
		CHECK(robin::GravatarResolver::NormalizeEmail("  Alice.Smith@Example.COM  ") == "alice.smith@example.com");

		// (the example of the documentation of Gravatar)
		const std::string hash = robin::GravatarResolver::EmailHash("MyEmailAddress@example.com ");
		CHECK(hash == "84059b07d4be67b806386c0aad8070a23f18836bbaae342275dc0a83414c32ee");
		CHECK(robin::GravatarResolver::LibravatarAvatarUrl(hash) == "https://seccdn.libravatar.org/avatar/" + hash + "?d=404&s=256");
		CHECK(robin::GravatarResolver::GravatarAvatarUrl(hash, 256) == "https://gravatar.com/avatar/" + hash + "?d=404&s=256");
		CHECK(robin::GravatarResolver::GravatarProfileUrl(hash) == "https://api.gravatar.com/v3/profiles/" + hash);
	}

	// 3. Gravatar profile JSON parsing
	{
		const char* sampleJson = R"({
			"display_name": "Alice Smith",
			"job_title": "Systems Hacker",
			"company": "SEN Laboratories",
			"current_location": "Vienna, Austria",
			"description": "Building semantic desktop systems on Haiku OS.",
			"urls": [
				{"value": "https://alicesmith.example.org"}
			],
			"verified_accounts": [
				{"url": "https://github.com/alicesmith"}
			]
		})";

		robin::PersonProfile profile;
		CHECK(robin::GravatarResolver::ParseProfileJson(sampleJson, &profile));
		CHECK(profile.name == "Alice Smith");
		CHECK(profile.jobTitle == "Systems Hacker");
		CHECK(profile.company == "SEN Laboratories");
		CHECK(profile.location == "Vienna, Austria");
		CHECK(profile.bio == "Building semantic desktop systems on Haiku OS.");
		CHECK(profile.homepage == "https://alicesmith.example.org");
		CHECK(profile.sameAs.size() == 1 && profile.sameAs[0] == "https://github.com/alicesmith");
	}

	// 4. GitHub parsing
	{
		CHECK(robin::GitHubResolver::ExtractUsername("@octocat") == "octocat");
		CHECK(robin::GitHubResolver::ExtractUsername("https://github.com/octocat") == "octocat");
		CHECK(robin::GitHubResolver::ExtractUsername("https://github.com/octocat?tab=repositories") == "octocat");
		CHECK(robin::GitHubResolver::DirectAvatarUrl("octocat") == "https://github.com/octocat.png");

		const char* ghSample = R"({
			"name": "The Octocat",
			"company": "GitHub",
			"blog": "https://github.blog",
			"location": "San Francisco",
			"bio": "Building the future of software",
			"html_url": "https://github.com/octocat",
			"avatar_url": "https://avatars.githubusercontent.com/u/583231"
		})";

		robin::PersonProfile ghProfile;
		CHECK(robin::GitHubResolver::ParseUserJson(ghSample, &ghProfile));
		CHECK(ghProfile.name == "The Octocat");
		CHECK(ghProfile.company == "GitHub");
		CHECK(ghProfile.jobTitle.empty());		// (the company of an account is no job title)
		CHECK(ghProfile.homepage == "https://github.blog");
		CHECK(ghProfile.location == "San Francisco");
		CHECK(ghProfile.bio == "Building the future of software");
		CHECK(ghProfile.avatarUrl == "https://avatars.githubusercontent.com/u/583231");
	}

	// 5. Text of feeds: CDATA, entities, HTML, Markdown
	{
		using namespace robin::feedtext;
		CHECK(UnwrapCdata("<![CDATA[My first post]]>") == "My first post");
		CHECK(UnwrapCdata("a <![CDATA[b]]> c <![CDATA[<d>]]>") == "a b c <d>");
		CHECK(DecodeEntities("A developer&#8217;s guide &amp; more &#x2019; &lt;b&gt; &amp;lt; &nbsp;&unknown; & done") == "A developer\xE2\x80\x99s guide & more \xE2\x80\x99 <b> &lt; \xC2\xA0&unknown; & done");
		CHECK(HtmlToPlainText("<![CDATA[<p>Hello <b>world</b></p><p>Second</p>]]>") == "Hello world Second");
		CHECK(HtmlToPlainText("a &lt;b&gt; c < d") == "a <b> c < d");
		CHECK(HtmlToPlainText("<a href=\"x>y\">link</a> text") == "link text");
		CHECK(HtmlToMarkdown("<p>Hello <strong>bold</strong> and <a href=\"https://ex.org/a?b=1&amp;c=2\">a link</a>.</p><p>Second<br>line</p>")
			== "Hello **bold** and [a link](https://ex.org/a?b=1&c=2).\n\nSecond\nline");
		CHECK(HtmlToMarkdown("<p>Pic <img src=\"https://ex.org/p.png\" alt=\"A picture\"></p><ul><li>one</li><li>two</li></ul>") == "Pic ![A picture](https://ex.org/p.png)\n\n- one\n- two");
		// (a cut is made at the end of a character, never in the middle of one)
		CHECK(TruncateUtf8("abcdef", 10) == "abcdef");
		CHECK(TruncateUtf8("r\xC3\xA4" "ubersuche mit Umlauten", 6) == "r\xC3\xA4\xE2\x80\xA6");
		CHECK(TruncateUtf8("\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4", 8) == "\xC3\xA4\xC3\xA4\xE2\x80\xA6");
		CHECK(FileNamePart("a/b: c\n d.", 40) == "a-b- c d");
	}

	// 6. Dates: RSS writes them one way, Atom and ActivityPub another
	{
		using namespace robin::feedtext;
		int64_t epoch = 0;
		CHECK(ParseDate("Fri, 06 Nov 2026 22:15:59 +0000", &epoch) && FormatIso(epoch) == "2026-11-06T22:15:59Z");
		CHECK(ParseDate("Fri, 06 Nov 2026 22:15:59 GMT", &epoch) && FormatIso(epoch) == "2026-11-06T22:15:59Z");
		CHECK(ParseDate("Sun, 01 Nov 2026 21:36:00 +0100", &epoch) && FormatIso(epoch) == "2026-11-01T20:36:00Z");
		CHECK(ParseDate("2 Jan 2006 15:04 EST", &epoch) && FormatIso(epoch) == "2006-01-02T20:04:00Z");
		CHECK(ParseDate("2026-11-08T18:00:00Z", &epoch) && epoch == 1794160800 && FormatIso(epoch) == "2026-11-08T18:00:00Z");
		CHECK(ParseDate("2026-11-08T19:00:00.123+01:00", &epoch) && FormatIso(epoch) == "2026-11-08T18:00:00Z");
		CHECK(ParseDate("2026-11-08", &epoch) && FormatIso(epoch) == "2026-11-08T00:00:00Z");
		CHECK(ParseDate("2024-02-29T12:00:00Z", &epoch) && FormatIso(epoch) == "2024-02-29T12:00:00Z");
		CHECK(!ParseDate("2026-02-30T12:00:00Z", &epoch));		// (not in the calendar)
		CHECK(!ParseDate("yesterday", &epoch));
		CHECK(!ParseDate("", &epoch));
		CHECK(FileDatePrefix(1794160800) == "2026-11-08 18.00");		// (sorts by date)
		CHECK(FormatIso(0) == "1970-01-01T00:00:00Z");
	}

	// 7. Bluesky names a post by an AT URI; the address on the web is made of the handle and the key
	{
		using namespace robin::feedtext;
		CHECK(WebUrl("at://did:plc:abc123/app.bsky.feed.post/3k44xyz", "alice.bsky.social") == "https://bsky.app/profile/alice.bsky.social/post/3k44xyz");
		CHECK(WebUrl("at://did:plc:abc123/app.bsky.feed.post/3k44xyz") == "https://bsky.app/profile/did:plc:abc123/post/3k44xyz");		// (bsky.app takes the DID, too)
		CHECK(WebUrl("at://did:plc:abc123", "alice.bsky.social") == "https://bsky.app/profile/alice.bsky.social");
		CHECK(WebUrl("at://did:plc:abc123/app.bsky.graph.list/xyz").empty());		// (not a post: no address on the web)
		CHECK(WebUrl("https://mastodon.social/@alice/1") == "https://mastodon.social/@alice/1");
	}

	// 8. Feeds
	{
		std::string html = "<html><head><LINK REL=\"stylesheet\" href=\"/a.css\"><link rel=\"alternate\" type=\"application/atom+xml\" href=\"/feed.xml\" /></head></html>";
		CHECK(robin::FeedResolver::DiscoverFeedFromHtml(html, "https://example.org") == "https://example.org/feed.xml");
		CHECK(robin::FeedResolver::DiscoverFeedFromHtml("<link rel='alternate' type='application/rss+xml' href='feed/'>", "https://example.org/blog/index.html") == "https://example.org/blog/feed/");
		CHECK(robin::FeedResolver::DiscoverFeedFromHtml("<link rel=\"alternate\" type=\"text/html\" href=\"/de/\">", "https://example.org").empty());

		CHECK(robin::FeedResolver::WebFingerUrl("@alice@mastodon.social") == "https://mastodon.social/.well-known/webfinger?resource=acct:alice@mastodon.social");
		CHECK(robin::FeedResolver::BlueskyFeedUrl("https://bsky.app/profile/alice.bsky.social/post/3k44") == "https://public.api.bsky.app/xrpc/app.bsky.feed.getAuthorFeed?actor=alice.bsky.social&limit=20&filter=posts_no_replies");
		CHECK(robin::FeedResolver::BlueskyRssUrl("https://bsky.app/profile/alice.bsky.social") == "https://bsky.app/profile/alice.bsky.social/rss");

		// RSS as WordPress writes it: CDATA around the title and the text, the time of RFC 822, an atom:link that is not the address of the post
		{
			robin::PersonProfile p;
			const char* rss = "<?xml version=\"1.0\"?><rss xmlns:atom=\"http://www.w3.org/2005/Atom\" xmlns:content=\"http://purl.org/rss/1.0/modules/content/\"><channel><title>Blog</title>"
				"<link>https://ex.org/</link>"
				"<item><title><![CDATA[My first post]]></title><link>https://ex.org/1</link><atom:link href=\"https://ex.org/1/?feed\" rel=\"self\"/><pubDate>Fri, 06 Nov 2026 22:15:59 +0000</pubDate>"
				"<description><![CDATA[<p>Short</p>]]></description><content:encoded><![CDATA[<p>Hello <b>world</b> &amp; all</p>]]></content:encoded></item>"
				"<item><title>Second &amp; &#8216;old&#8217;</title><link>https://ex.org/2</link><pubDate>Sun, 01 Nov 2026 21:36:00 +0000</pubDate><description>&lt;p&gt;Escaped &lt;i&gt;HTML&lt;/i&gt;&lt;/p&gt;</description></item>"
				"</channel></rss>";
			CHECK(robin::FeedResolver::ParseRssXml(rss, &p));
			CHECK(p.recentPosts.size() == 2);
			CHECK(p.recentPosts[0].title == "My first post");
			CHECK(p.recentPosts[0].url == "https://ex.org/1");
			CHECK(p.recentPosts[0].contentMarkdown == "Hello **world** & all");
			CHECK(p.recentPosts[0].epoch == 1794003359);		// (2026-11-06T22:15:59Z)
			CHECK(p.recentPosts[1].title == "Second & \xE2\x80\x98old\xE2\x80\x99");
			CHECK(p.recentPosts[1].contentMarkdown == "Escaped *HTML*");
			CHECK(p.latestStatus == "My first post" && p.statusEpoch == p.recentPosts[0].epoch);
		}

		// Atom: the address is the href of the link, the text is HTML
		{
			robin::PersonProfile p;
			const char* atom = "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>x</title><link href=\"https://ex.org/\"/>"
				"<entry><title type=\"html\">Atom &amp; post</title><link rel=\"edit\" href=\"https://ex.org/edit/a\"/><link rel=\"alternate\" href=\"https://ex.org/a\"/>"
				"<published>2026-11-02T10:00:00Z</published><content type=\"html\">&lt;p&gt;Hi&lt;/p&gt;</content></entry></feed>";
			CHECK(robin::FeedResolver::ParseRssXml(atom, &p) && p.recentPosts.size() == 1);
			CHECK(p.recentPosts[0].title == "Atom & post" && p.recentPosts[0].url == "https://ex.org/a" && p.recentPosts[0].contentMarkdown == "Hi");
		}

		// a long feed is no risk: it is looked through, not matched by a regular expression
		{
			robin::PersonProfile p;
			std::string big = "<rss><channel>";
			for (int i = 0; i < 3000; i++)
				big += "<item><title>t" + std::to_string(i) + "</title><description>" + std::string(400, 'x') + "</description></item>";
			big += "</channel></rss>";
			CHECK(robin::FeedResolver::ParseRssXml(big, &p) && p.recentPosts.size() == 20);
		}

		// ActivityPub: the outbox of Mastodon is the collection, its items are on the page; boosts and replies are not what the person says
		{
			const char* collection = R"({"type":"OrderedCollection","totalItems":120,"first":"https://m.example/users/a/outbox?page=true","last":"https://m.example/users/a/outbox?min_id=0&page=true"})";
			CHECK(robin::FeedResolver::OutboxFirstPageUrl(collection) == "https://m.example/users/a/outbox?page=true");
			robin::PersonProfile none;
			CHECK(!robin::FeedResolver::ParseOutboxJson(collection, &none));

			const char* page = R"({"type":"OrderedCollectionPage","orderedItems":[
				{"type":"Announce","object":"https://other.example/@b/9"},
				{"type":"Create","object":{"type":"Note","published":"2026-11-08T18:00:00Z","url":"https://m.example/@a/111","content":"<p>First <a href=\"https://ex.org\">link</a></p>","inReplyTo":null,
					"attachment":[{"type":"Document","url":"https://m.example/media/1.png"}]}},
				{"type":"Create","object":{"type":"Note","published":"2026-11-08T19:00:00Z","url":"https://m.example/@a/112","content":"<p>A reply</p>","inReplyTo":"https://m.example/@b/1"}},
				{"type":"Create","object":{"type":"Note","published":"2026-11-07T08:00:00Z","url":"https://m.example/@a/110","content":"<p>Older</p>"}}
			]})";
			CHECK(robin::FeedResolver::OutboxFirstPageUrl(page).empty());
			robin::PersonProfile p;
			CHECK(robin::FeedResolver::ParseOutboxJson(page, &p) && p.recentPosts.size() == 2);
			CHECK(p.recentPosts[0].url == "https://m.example/@a/111" && p.recentPosts[0].contentMarkdown == "First [link](https://ex.org)");
			CHECK(p.recentPosts[0].mediaUrl == "https://m.example/media/1.png");
			CHECK(p.latestStatus == "First link" && p.statusEpoch == 1794160800);
		}

		// Bluesky: no reposts, and the address of a post is on the web (an AT URI opens nowhere)
		{
			const char* feed = R"({"feed":[
				{"post":{"uri":"at://did:plc:abc/app.bsky.feed.post/3k44aaa","author":{"handle":"alice.bsky.social"},"record":{"text":"Hello from Haiku","createdAt":"2026-11-08T18:00:00.000Z"},
					"embed":{"images":[{"fullsize":"https://cdn.bsky.app/img/1.jpg"}]}}},
				{"post":{"uri":"at://did:plc:xyz/app.bsky.feed.post/3k44bbb","author":{"handle":"bob.bsky.social"},"record":{"text":"Not hers","createdAt":"2026-11-08T19:00:00.000Z"}},
					"reason":{"$type":"app.bsky.feed.defs#reasonRepost"}}
			]})";
			robin::PersonProfile p;
			CHECK(robin::FeedResolver::ParseBlueskyFeedJson(feed, &p) && p.recentPosts.size() == 1);
			CHECK(p.recentPosts[0].url == "https://bsky.app/profile/alice.bsky.social/post/3k44aaa");
			CHECK(p.recentPosts[0].mediaUrl == "https://cdn.bsky.app/img/1.jpg" && p.statusEpoch == 1794160800);
			const std::string md = robin::FeedResolver::FormatPostToMarkdown(p.recentPosts[0], "Alice");
			CHECK(md.find("[Original Post](https://bsky.app/profile/alice.bsky.social/post/3k44aaa)") != std::string::npos);
			CHECK(md.find("at://") == std::string::npos);
			CHECK(md.find("*By Alice on 2026-11-08T18:00:00Z*") != std::string::npos);

			robin::PersonProfile account;
			CHECK(robin::FeedResolver::ParseBlueskyProfileJson(R"({"avatar":"https://cdn.bsky.app/a.jpg","description":"Haiku fan","displayName":"Alice"})", &account));
			CHECK(account.avatarUrl == "https://cdn.bsky.app/a.jpg" && account.bio == "Haiku fan");
		}

		robin::FeedPost post;
		post.title = "Robin Prototype Finished";
		post.published = "2026-11-08T18:00:00Z";
		post.epoch = 1794160800;
		post.contentMarkdown = "Enriching Haiku People with avatars and feeds!";
		post.url = "https://mastodon.social/@alice/12345";
		post.mediaUrl = "https://example.org/photo.png";

		std::string md = robin::FeedResolver::FormatPostToMarkdown(post, "Alice");
		CHECK(md.find("# Robin Prototype Finished") != std::string::npos);
		CHECK(md.find("![Robin Prototype Finished](https://example.org/photo.png)") != std::string::npos);
		CHECK(md.find("[Original Post](https://mastodon.social/@alice/12345)") != std::string::npos);
	}

	// 9. Who a profile is: by the links of the file, never by a name that happens to be the same
	{
		robin::PersonProfile nickOnly;
		nickOnly.nick = "octocat";
		CHECK(robin::PersonEnricher::GitHubUser(nickOnly).empty());
		CHECK(robin::PersonEnricher::FediverseHandle(nickOnly).empty());

		robin::PersonProfile linked;
		linked.sameAs = {"https://github.com/octocat", "https://mastodon.social/@alice/", "https://bsky.app/profile/alice.bsky.social"};
		CHECK(robin::PersonEnricher::GitHubUser(linked) == "octocat");
		CHECK(robin::PersonEnricher::FediverseHandle(linked) == "alice@mastodon.social");
		CHECK(robin::PersonEnricher::BlueskyLink(linked) == "https://bsky.app/profile/alice.bsky.social");

		robin::PersonProfile handle;
		handle.nick = "@alice@hachyderm.io";
		CHECK(robin::PersonEnricher::FediverseHandle(handle) == "alice@hachyderm.io");
		robin::PersonProfile mail;
		mail.nick = "alice@example.com";		// (an address: not a handle)
		CHECK(robin::PersonEnricher::FediverseHandle(mail).empty());
		robin::PersonProfile other;
		other.sameAs = {"https://medium.com/@alice", "https://github.com/orgs/haiku", "https://github.com/search?q=x"};
		CHECK(robin::PersonEnricher::FediverseHandle(other).empty());
		CHECK(robin::PersonEnricher::GitHubUser(other).empty());
	}

	// 10. The cascade with scripted services
	{
		// a nickname alone asks nobody: there is no account that it could be
		{
			FakeWeb web;
			robin::PersonEnricher enricher(web.Get());
			robin::PersonProfile p;
			p.name = "Mona Lisa";
			p.nick = "octocat";
			CHECK(!enricher.Enrich(&p));
			CHECK(web.asked.empty());
		}

		// a Fediverse account: WebFinger, the actor (asked for ActivityPub, as Mastodon wants), the photo, the outbox and its first page
		{
			FakeWeb web;
			web.answers["https://m.example/.well-known/webfinger?resource=acct:alice@m.example"] =
				R"({"subject":"acct:alice@m.example","links":[{"rel":"self","type":"application/activity+json","href":"https://m.example/users/alice"}]})";
			web.answers["https://m.example/users/alice"] =
				R"({"name":"Alice","summary":"<p>Haiku fan</p>","icon":{"type":"Image","url":"https://m.example/a.png"},"outbox":"https://m.example/users/alice/outbox","url":"https://m.example/@alice"})";
			web.answers["https://m.example/a.png"] = "PNGDATA";
			web.answers["https://m.example/users/alice/outbox"] = R"({"type":"OrderedCollection","totalItems":3,"first":"https://m.example/users/alice/outbox?page=true"})";
			web.answers["https://m.example/users/alice/outbox?page=true"] =
				R"({"orderedItems":[{"type":"Create","object":{"type":"Note","published":"2026-11-08T18:00:00Z","url":"https://m.example/@alice/1","content":"<p>Hello</p>"}}]})";
			robin::PersonEnricher enricher(web.Get());
			robin::PersonProfile p;
			p.name = "Alice";
			p.sameAs = {"https://m.example/@alice"};
			CHECK(enricher.Enrich(&p));
			CHECK(p.HasAvatar() && p.avatarCredit == "Fediverse" && p.bio == "Haiku fan");
			CHECK(p.feedUrl == "https://m.example/users/alice/outbox");
			CHECK(p.recentPosts.size() == 1 && p.latestStatus == "Hello" && p.statusEpoch == 1794160800);
			CHECK(web.accepts["https://m.example/users/alice"].find("activity+json") != std::string::npos);
			CHECK(web.accepts["https://m.example/users/alice/outbox"].find("activity+json") != std::string::npos);
			CHECK(web.accepts["https://m.example/users/alice/outbox?page=true"].find("activity+json") != std::string::npos);
			CHECK(!web.Asked("github"));

			// the posts alone (the navigator): the feed is known, nothing else is asked
			web.asked.clear();
			CHECK(enricher.RefreshPosts(&p) && p.recentPosts.size() == 1);
			CHECK(!web.Asked("webfinger") && !web.Asked("a.png"));
		}

		// Bluesky: the profile for the photo, the author feed for the posts; the feed that is kept for the person is the RSS feed
		{
			FakeWeb web;
			web.answers["https://public.api.bsky.app/xrpc/app.bsky.actor.getProfile?actor=alice.bsky.social"] = R"({"avatar":"https://cdn.bsky.app/a.jpg","description":"Haiku fan"})";
			web.answers["https://cdn.bsky.app/a.jpg"] = "JPGDATA";
			web.answers["https://public.api.bsky.app/xrpc/app.bsky.feed.getAuthorFeed?actor=alice.bsky.social&limit=20&filter=posts_no_replies"] =
				R"({"feed":[{"post":{"uri":"at://did:plc:abc/app.bsky.feed.post/3k44aaa","author":{"handle":"alice.bsky.social"},"record":{"text":"Hi","createdAt":"2026-11-08T18:00:00Z"}}}]})";
			robin::PersonEnricher enricher(web.Get());
			robin::PersonProfile p;
			p.name = "Alice";
			p.sameAs = {"https://bsky.app/profile/alice.bsky.social"};
			CHECK(enricher.Enrich(&p));
			CHECK(p.HasAvatar() && p.avatarCredit == "Bluesky" && p.bio == "Haiku fan");
			CHECK(p.feedUrl == "https://bsky.app/profile/alice.bsky.social/rss");
			CHECK(p.recentPosts.size() == 1 && p.recentPosts[0].url == "https://bsky.app/profile/alice.bsky.social/post/3k44aaa");
		}

		// the links that say who a person is are read from wherever the person has them: the homepage (the only place of a contact of the People application) counts as the links do
		{
			robin::PersonProfile p;
			p.homepage = "https://bsky.app/profile/alice.bsky.social";
			CHECK(robin::PersonEnricher::BlueskyLink(p) == "https://bsky.app/profile/alice.bsky.social");
			robin::PersonProfile q;
			q.homepage = "https://github.com/alice";
			CHECK(robin::PersonEnricher::GitHubUser(q) == "alice");
			robin::PersonProfile r;
			r.nick = "@alice.bsky.social";		// (a handle of Bluesky as nickname)
			CHECK(robin::PersonEnricher::BlueskyLink(r) == "https://bsky.app/profile/alice.bsky.social");
			r.nick = "@alice@m.example";
			CHECK(robin::PersonEnricher::BlueskyLink(r).empty() && robin::PersonEnricher::FediverseHandle(r) == "alice@m.example");
		}

		// a link of Gravatar: the profile by its name, or the avatar by its hash; the pages of the site that are no profile are not
		{
			robin::PersonProfile p;
			p.sameAs = {"https://gravatar.com/avatar/84059b07d4be67b806386c0aad8070a23f18836bbb743dae0d8a1f2a5bf4b3d4?s=200", "https://example.org/matt"};
			CHECK(robin::PersonEnricher::GravatarId(p) == "84059b07d4be67b806386c0aad8070a23f18836bbb743dae0d8a1f2a5bf4b3d4");
			p.sameAs = {"https://en.gravatar.com/matt.json"};
			CHECK(robin::PersonEnricher::GravatarId(p) == "matt");
			p.sameAs = {"https://gravatar.com/profiles/x", "https://gravatar.com/", "https://gravatar.com/avatar/notahash"};
			CHECK(robin::PersonEnricher::GravatarId(p).empty());
			robin::PersonProfile q;
			q.homepage = "https://gravatar.com/matt";
			CHECK(robin::PersonEnricher::GravatarId(q) == "matt");

			// the enrichment asks the profile of the service by the name, and the photo that it names
			FakeWeb web;
			web.answers["https://api.gravatar.com/v3/profiles/matt"] = R"({"display_name":"Matt","description":"Hello from Gravatar","avatar_url":"https://0.gravatar.com/avatar/abc"})";
			web.answers["https://0.gravatar.com/avatar/abc"] = "IMG";
			robin::PersonEnricher enricher(web.Get());
			robin::PersonProfile m;
			m.name = "Matt";
			m.sameAs = {"https://gravatar.com/matt"};
			CHECK(enricher.Enrich(&m));
			CHECK(web.Asked("api.gravatar.com/v3/profiles/matt"));
		}

		// what the enrichment says when it finds nothing: where it looked, or what the person lacks
		{
			robin::PersonProfile p;
			CHECK(robin::PersonEnricher::Explain(p).find("nothing to look up") != std::string::npos);
			p.mbox = "a@b.example";
			p.sameAs = {"https://github.com/alice", "https://bsky.app/profile/alice.bsky.social"};
			const std::string text = robin::PersonEnricher::Explain(p);
			CHECK(text.find("Gravatar") != std::string::npos && text.find("GitHub (alice)") != std::string::npos && text.find("Bluesky") != std::string::npos);
			robin::Settings off;
			off.github = false;
			CHECK(robin::PersonEnricher::Explain(p, off).find("GitHub") == std::string::npos);
		}

		// ---- the hunt: handles, names, the answers of the services, the scores
		{
			using robin::Hunter;
			using robin::Handle;
			// handles: with or without the @, a server of the Fediverse, a domain (Bluesky), a plain name; written back in one form
			CHECK(Hunter::ParseHandle("@alice@m.example").kind == Handle::kFediverse && Hunter::ParseHandle("alice@M.Example").text == "@alice@m.example");
			CHECK(Hunter::ParseHandle("alice@m.example").user == "alice" && Hunter::ParseHandle("alice@m.example").host == "m.example");
			CHECK(Hunter::ParseHandle("@alice.bsky.social").kind == Handle::kDomain && Hunter::ParseHandle("@alice.bsky.social").text == "alice.bsky.social");
			CHECK(Hunter::ParseHandle("sen-labs.org").kind == Handle::kDomain);			// (a domain of one's own is a handle of Bluesky)
			CHECK(Hunter::ParseHandle("  @alice  ").kind == Handle::kPlain && Hunter::ParseHandle("@alice").text == "alice");
			CHECK(Hunter::ParseHandle("").kind == Handle::kNone && Hunter::ParseHandle("two words").kind == Handle::kNone && Hunter::ParseHandle("https://x.org/a").kind == Handle::kNone);
			CHECK(Hunter::ParseHandle("a@b@c.example").kind == Handle::kNone && Hunter::ParseHandle("@nobody@").kind == Handle::kNone);

			// names: accents, case and the order of the words do not count, a middle initial costs little, another name is nothing
			CHECK(Hunter::NameSimilarity("Gregor Rosenauer", "gregor rosenauer") == 1.0f);
			CHECK(Hunter::NameSimilarity("Rosenauer, Gregor", "Gregor Rosenauer") == 1.0f);
			CHECK(Hunter::NameSimilarity("Jos\xC3\xA9 Garc\xC3\xAD" "a", "Jose Garcia") == 1.0f);
			CHECK(Hunter::NameSimilarity("Gregor B. Rosenauer", "Gregor Rosenauer") > 0.85f && Hunter::NameSimilarity("Gregor B. Rosenauer", "Gregor Rosenauer") < 1.0f);
			CHECK(Hunter::NameSimilarity("Gregor Rosenauer", "Gregor Poynton") < 0.5f && Hunter::NameSimilarity("Gregor", "Alice") == 0);
			CHECK(Hunter::UrlEncode("Gregor Rosenauer") == "Gregor%20Rosenauer" && Hunter::UrlEncode("a+b/c") == "a%2Bb%2Fc");

			// Bluesky: the search and a profile
			std::vector<robin::HuntHit> hits;
			CHECK(Hunter::ParseBlueskySearch(R"({"actors":[{"did":"did:plc:1","handle":"gregorpoynton.bsky.social","displayName":"Gregor Poynton","description":"Hello","avatar":"https://cdn/a.jpg"},{"did":"did:plc:2","handle":"sen-labs.org"}]})", &hits));
			CHECK(hits.size() == 2 && hits[0].service == "bluesky" && hits[0].name == "Gregor Poynton" && hits[0].url == "https://bsky.app/profile/gregorpoynton.bsky.social");
			CHECK(hits[1].name == "sen-labs.org" && hits[1].avatarUrl.empty());
			robin::HuntHit profileHit;
			CHECK(Hunter::ParseBlueskyProfile(R"({"handle":"sen-labs.org","displayName":"SEN Labs","followersCount":120})", &profileHit) && profileHit.followers == 120);
			CHECK(!Hunter::ParseBlueskyProfile(R"({"error":"InvalidRequest"})", &profileHit));

			// Mastodon: the account of another server keeps its handle, one of the server that was asked gets the server; a link of a field is a page of the person
			hits.clear();
			CHECK(Hunter::ParseMastodonSearch(R"({"accounts":[{"username":"gregor","acct":"gregor@toot.cafe","display_name":"Gregor","url":"https://toot.cafe/@gregor","note":"<p>he/him. Haiku</p>","avatar":"https://x/a.png","followers_count":40,)"
				R"("fields":[{"name":"Web","value":"<a href=\"https://gregor.example\" rel=\"me\">gregor.example</a>"}]},{"username":"alice","acct":"alice","display_name":"","url":"https://m.example/@alice","note":""}]})", "m.example", &hits));
			CHECK(hits.size() == 2 && hits[0].id == "gregor@toot.cafe" && hits[0].description == "he/him. Haiku" && hits[0].links.size() == 1 && hits[0].links[0] == "https://gregor.example");
			CHECK(hits[1].id == "alice@m.example" && hits[1].name == "alice");
			// a mirror of a Bluesky account on the bridge is not an answer of its own
			hits.clear();
			CHECK(Hunter::ParseMastodonSearch(R"({"accounts":[{"username":"timbl.bsky.social","acct":"timbl.bsky.social@bsky.brid.gy","url":"https://bsky.brid.gy/r/x","note":""}]})", "m.example", &hits));
			CHECK(hits.empty());

			// GitHub: the answer of the search has the login only (an organization is no person), the user has the rest
			hits.clear();
			CHECK(Hunter::ParseGitHubSearch(R"({"items":[{"login":"grexe","html_url":"https://github.com/grexe","avatar_url":"https://a/g.png","type":"User"},{"login":"acme","type":"Organization"}]})", &hits));
			CHECK(hits.size() == 1 && hits[0].id == "grexe");
			robin::HuntHit user;
			CHECK(Hunter::ParseGitHubUser(R"({"login":"grexe","name":"Gregor B. Rosenauer","html_url":"https://github.com/grexe","bio":"Haiku","location":"Vienna","company":"SEN Labs","blog":"sen-labs.org","twitter_username":"grexe","followers":30,"type":"User"})", &user));
			CHECK(user.name == "Gregor B. Rosenauer" && user.location == "Vienna" && user.homepage == "https://sen-labs.org" && user.links.size() == 2);

			// Wikidata: only people (Q5); the photo from Commons, the ORCID iD and the accounts that the entry knows
			std::vector<std::string> ids;
			CHECK(Hunter::ParseWikidataSearch(R"({"search":[{"id":"Q80","label":"Tim Berners-Lee"},{"id":"Q1"}]})", &ids) && ids.size() == 2);
			hits.clear();
			CHECK(Hunter::ParseWikidataEntities(R"({"entities":{"Q80":{"id":"Q80","labels":{"en":{"value":"Tim Berners-Lee"}},"descriptions":{"en":{"value":"English computer scientist"}},"claims":{)"
				R"("P31":[{"mainsnak":{"datavalue":{"value":{"id":"Q5"}}}}],"P18":[{"mainsnak":{"datavalue":{"value":"Tim Berners-Lee 2012.jpg"}}}],)"
				R"("P496":[{"mainsnak":{"datavalue":{"value":"0000-0003-1279-3709"}}}],"P2037":[{"mainsnak":{"datavalue":{"value":"timbl"}}}],"P4033":[{"mainsnak":{"datavalue":{"value":"@timbl@w3c.social"}}}]}},)"
				R"("Q1":{"id":"Q1","labels":{"en":{"value":"Universe"}},"claims":{"P31":[{"mainsnak":{"datavalue":{"value":{"id":"Q36906466"}}}}]}}}})", &hits));
			CHECK(hits.size() == 1 && hits[0].wikidata == "Q80" && hits[0].orcid == "0000-0003-1279-3709" && hits[0].url == "https://www.wikidata.org/wiki/Q80");
			CHECK(hits[0].avatarUrl == "https://commons.wikimedia.org/wiki/Special:FilePath/Tim_Berners-Lee_2012.jpg?width=512");
			CHECK(std::find(hits[0].links.begin(), hits[0].links.end(), "https://github.com/timbl") != hits[0].links.end());
			CHECK(std::find(hits[0].links.begin(), hits[0].links.end(), "https://w3c.social/@timbl") != hits[0].links.end());

			// ORCID: the iDs of the search, the record of one
			ids.clear();
			CHECK(Hunter::ParseOrcidSearch(R"({"result":[{"orcid-identifier":{"path":"0000-0002-9322-3515"}}],"num-found":1})", &ids) && ids.size() == 1);
			robin::HuntHit researcher;
			CHECK(Hunter::ParseOrcidPerson(R"({"name":{"given-names":{"value":"Yoshua"},"family-name":{"value":"Bengio"}},"biography":{"content":"AI"},)"
				R"("researcher-urls":{"researcher-url":[{"url":{"value":"https://yoshuabengio.org"}}]}})", "0000-0002-9322-3515", &researcher));
			CHECK(researcher.name == "Yoshua Bengio" && researcher.url == "https://orcid.org/0000-0002-9322-3515" && researcher.homepage == "https://yoshuabengio.org");

			// scores: a name alone is not enough to be sure, the handle or a link of the file settles it, what else the file says helps
			robin::PersonProfile wanted;
			wanted.name = "Gregor Rosenauer";
			robin::HuntHit byName;
			byName.service = "github"; byName.id = "grexe"; byName.name = "Gregor Rosenauer"; byName.url = "https://github.com/grexe";
			CHECK(Hunter::Score(&byName, wanted) < 0.8f && byName.score > 0.6f);
			robin::HuntHit other = byName;
			other.name = "Gregor Poynton"; other.id = "gp";
			CHECK(Hunter::Score(&other, wanted) < 0.4f);
			wanted.nick = "@grexe";
			CHECK(Hunter::Score(&byName, wanted) >= 0.9f);
			wanted.nick.clear();
			wanted.sameAs = {"https://www.github.com/grexe/"};
			CHECK(Hunter::Score(&byName, wanted) >= 0.9f);
			wanted.sameAs.clear();
			wanted.location = "Vienna";
			byName.location = "Vienna, Austria";
			const float withPlace = Hunter::Score(&byName, wanted);
			byName.location.clear();
			CHECK(withPlace > Hunter::Score(&byName, wanted));
			wanted.nick = "sen-labs.org";			// (a domain handle matches the Bluesky account of that name)
			robin::HuntHit sen;
			sen.service = "bluesky"; sen.id = "sen-labs.org"; sen.name = "SEN Labs";
			CHECK(Hunter::Score(&sen, wanted) >= 0.85f);

			// a clear winner needs 0.8 and a gap of 0.25
			std::vector<robin::HuntHit> ranked(2);
			ranked[0].score = 0.9f; ranked[1].score = 0.5f;
			CHECK(Hunter::IsClearWinner(ranked));
			ranked[1].score = 0.8f;
			CHECK(!Hunter::IsClearWinner(ranked));
			ranked.resize(1); ranked[0].score = 0.7f;
			CHECK(!Hunter::IsClearWinner(ranked) && !Hunter::IsClearWinner({}));
		}

		// the search through the services that are on, in the order of the settings, with a handle that is a domain
		{
			FakeWeb web;
			web.answers["https://public.api.bsky.app/xrpc/app.bsky.actor.getProfile?actor=sen-labs.org"] = R"({"handle":"sen-labs.org","displayName":"SEN Labs","description":"Semantic Extensions Native"})";
			web.answers["https://public.api.bsky.app/xrpc/app.bsky.actor.searchActors?limit=10&q=SEN%20Labs"] = R"({"actors":[{"handle":"sen-labs.org","displayName":"SEN Labs"},{"handle":"other.bsky.social","displayName":"Senior Labs"}]})";
			robin::PersonProfile wanted;
			wanted.name = "SEN Labs";
			wanted.nick = "@sen-labs.org";
			robin::Settings settings;
			settings.huntSet = true;
			settings.huntOrder = {"github", "bluesky"};
			settings.huntOn = {"bluesky"};								// (GitHub is off, Bluesky on)
			robin::Hunter hunter(web.Get(), settings);
			const std::vector<robin::HuntHit> hits = hunter.Search(wanted);
			CHECK(!hits.empty() && hits[0].id == "sen-labs.org" && hits[0].score >= 0.9f && robin::Hunter::IsClearWinner(hits));
			CHECK(!web.Asked("github") && !web.Asked("mastodon") && !web.Asked("wikidata"));		// (what is off is never asked)

			// the settings: the order and the switches of the services, `-name` keeps a place
			robin::Settings parsed;
			robin::ParseSettings("hunt = wikidata -bluesky github bogus\nmastodon_servers = a.example b.example c\n", &parsed);
			CHECK((parsed.HuntServices() == std::vector<std::string>{"wikidata", "github"}));
			CHECK((parsed.HuntOrder() == std::vector<std::string>{"wikidata", "bluesky", "github", "mastodon", "orcid"}));
			CHECK((parsed.mastodonServers == std::vector<std::string>{"a.example", "b.example"}));
			CHECK((robin::Settings().HuntServices() == robin::AllHuntServices()));
		}

		// a homepage with a feed: found in the page, read as RSS
		{
			FakeWeb web;
			web.answers["https://alice.example/"] = "<html><head><link rel=\"alternate\" type=\"application/rss+xml\" href=\"/feed.xml\"></head></html>";
			web.answers["https://alice.example/feed.xml"] = "<rss><channel><item><title><![CDATA[Post]]></title><link>https://alice.example/p</link><pubDate>Fri, 06 Nov 2026 22:15:59 +0000</pubDate></item></channel></rss>";
			robin::PersonEnricher enricher(web.Get());
			robin::PersonProfile p;
			p.name = "Alice";
			p.homepage = "https://alice.example/";
			CHECK(enricher.Enrich(&p));
			CHECK(p.feedUrl == "https://alice.example/feed.xml" && p.recentPosts.size() == 1 && p.latestStatus == "Post");
		}
	}

	// 11. Settings: which services may be asked, what a photo gets
	{
		robin::Settings defaults;
		CHECK(defaults.emailServices && defaults.github && defaults.fediverse && defaults.bluesky && defaults.feeds && defaults.photo && defaults.photoSize == 256);

		robin::Settings s;
		robin::ParseSettings("# my settings\n email_services = no\ngithub=off\nfediverse = yes\nphoto_size = 128 # smaller\nunknown = kept\nbluesky = maybe\nphoto = 0\n", &s);
		CHECK(!s.emailServices && !s.github && s.fediverse && s.bluesky && s.feeds && !s.photo && s.photoSize == 128);		// ("maybe" is no answer: what was there stays)
		robin::Settings bad;
		robin::ParseSettings("photo_size = 5000\n", &bad);
		CHECK(bad.photoSize == 256);
		robin::ParseSettings("photo_size = 12.5\n", &bad);
		CHECK(bad.photoSize == 256);
		robin::ParseSettings("photo_size = 64\n", &bad);
		CHECK(bad.photoSize == 64);

		std::string why;
		CHECK(robin::ValidateSetting("github", "yes") && robin::ValidateSetting("photo_size", "512"));
		CHECK(!robin::ValidateSetting("github", "sure", &why) && why == "yes or no");
		CHECK(!robin::ValidateSetting("photo_size", "63", &why) && !robin::ValidateSetting("photo_size", "513") && !robin::ValidateSetting("photo_size", "x"));
		CHECK(!robin::ValidateSetting("no_such", "1", &why) && why == "no such setting");
		CHECK(!robin::ValidateSetting("feeds", "yes # no", &why));
	}

	// 12. A service that is switched off is never asked
	{
		const char* answers[] = {nullptr};
		(void)answers;
		auto person = []() {
			robin::PersonProfile p;
			p.name = "Alice";
			p.mbox = "alice@example.com";
			p.homepage = "https://alice.example/";
			p.sameAs = {"https://github.com/alice", "https://m.example/@alice", "https://bsky.app/profile/alice.bsky.social"};
			return p;
		};
		FakeWeb web;
		robin::Settings none;
		none.emailServices = none.github = none.fediverse = none.bluesky = none.feeds = false;
		robin::PersonEnricher quiet(web.Get(), none);
		robin::PersonProfile p = person();
		quiet.Enrich(&p);
		quiet.RefreshPosts(&p);
		CHECK(web.asked.empty());

		// one at a time
		for (int which = 0; which < 5; which++) {
			FakeWeb one;
			robin::Settings only = none;
			bool* flags[] = {&only.emailServices, &only.github, &only.fediverse, &only.bluesky, &only.feeds};
			*flags[which] = true;
			robin::PersonEnricher enricher(one.Get(), only);
			robin::PersonProfile q = person();
			enricher.Enrich(&q);
			const char* hosts[] = {"gravatar", "api.github.com", "m.example", "bsky.app", "alice.example"};
			for (int host = 0; host < 5; host++) {
				const bool allowed = host == which || (which == 3 && host == 3);
				CHECK(one.Asked(hosts[host]) == allowed);
			}
		}
	}

	// 12b. No photo wanted: none is downloaded, the rest is still asked
	{
		FakeWeb web;
		web.answers["https://api.github.com/users/alice"] = R"({"name":"Alice","company":"Haiku","avatar_url":"https://avatars.example/a","html_url":"https://github.com/alice"})";
		web.answers["https://github.com/alice.png"] = "PNG";
		robin::Settings noPhoto;
		noPhoto.photo = false;
		robin::PersonEnricher enricher(web.Get(), noPhoto);
		robin::PersonProfile p;
		p.name = "Alice";
		p.sameAs = {"https://github.com/alice"};
		CHECK(enricher.Enrich(&p) && !p.HasAvatar() && p.company == "Haiku");
		CHECK(web.Asked("api.github.com") && !web.Asked(".png") && !web.Asked("avatars.example"));
	}

	// 13. Photos are made smaller: the aspect is kept, nothing larger is made, averaging is a box filter
	{
		int w = 0, h = 0;
		sensei::ScaledSize(1000, 500, 256, &w, &h);
		CHECK(w == 256 && h == 128);
		sensei::ScaledSize(300, 1200, 256, &w, &h);
		CHECK(w == 64 && h == 256);
		sensei::ScaledSize(100, 100, 256, &w, &h);
		CHECK(w == 100 && h == 100);
		sensei::ScaledSize(256, 256, 256, &w, &h);
		CHECK(w == 256 && h == 256);
		sensei::ScaledSize(2000, 3, 256, &w, &h);
		CHECK(w == 256 && h == 1);		// (3 / 2000 of 256 is less than a pixel: one)
		sensei::ScaledSize(5000, 1, 100, &w, &h);
		CHECK(w == 100 && h == 1);		// (at least one pixel)

		// 4 x 4 to 2 x 2: each pixel is the average of four
		std::vector<uint8_t> source(4 * 4 * 4, 0);
		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				for (int c = 0; c < 4; c++)
					source[(y * 4 + x) * 4 + c] = (uint8_t)((x < 2) == (y < 2) ? 200 : 100);
			}
		}
		std::vector<uint8_t> two = sensei::BoxScale(source, 4, 4, 2, 2);
		CHECK(two.size() == 16 && two[0] == 200 && two[4] == 100 && two[8] == 100 && two[12] == 200);
		// a checkerboard becomes grey, and a uniform picture stays what it is
		std::vector<uint8_t> board(4 * 4 * 4);
		for (int i = 0; i < 16; i++) {
			for (int c = 0; c < 4; c++)
				board[i * 4 + c] = ((i % 4 + i / 4) % 2) ? 255 : 0;
		}
		const std::vector<uint8_t> grey = sensei::BoxScale(board, 4, 4, 1, 1);
		CHECK(grey.size() == 4 && (grey[0] == 127 || grey[0] == 128));
		std::vector<uint8_t> flat(7 * 5 * 4, 77);
		const std::vector<uint8_t> small = sensei::BoxScale(flat, 7, 5, 3, 2);
		CHECK(small.size() == 3 * 2 * 4 && small[0] == 77 && small[23] == 77);
		// (a size that is no whole fraction of the original: the edge pixels count by the part of them that is covered)
		std::vector<uint8_t> ramp(5 * 1 * 4, 0);
		for (int x = 0; x < 5; x++)
			for (int c = 0; c < 4; c++)
				ramp[x * 4 + c] = (uint8_t)(x * 50);
		const std::vector<uint8_t> two2 = sensei::BoxScale(ramp, 5, 1, 2, 1);
		CHECK(two2[0] == 40 && two2[4] == 160);		// ((0 + 50 + half of 100) / 2.5; (half of 100 + 150 + 200) / 2.5)
	}

	std::printf("Completed %d checks: %d failure(s).\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
