/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace robin {

/**
 * Text, dates and addresses of what a feed says (portable: no Haiku headers, so the tests run anywhere).
 */
namespace feedtext {

/** The text of an XML element that is wrapped in CDATA (`<![CDATA[...]]>`) without the wrapper; other text as it is. */
std::string UnwrapCdata(std::string_view text);

/** Decodes the entities of HTML and XML: the named ones that feeds use, and numeric ones (`&#8217;`, `&#x2019;`) as UTF-8. `&amp;` last, so that `&amp;lt;` is `&lt;`. */
std::string DecodeEntities(std::string_view text);

/** The text of a piece of HTML in one line: the tags are gone, the entities decoded, the white space made single. */
std::string HtmlToPlainText(std::string_view html);

/** A piece of HTML as Markdown: paragraphs and line breaks, links, images, lists, strong and emphasis; the rest of the tags are dropped. */
std::string HtmlToMarkdown(std::string_view html);

/** At most `maxBytes` of the text, cut at the end of a character of UTF-8 (never in the middle of one), with an ellipsis if something was cut. */
std::string TruncateUtf8(const std::string& text, size_t maxBytes);

/**
 * A time as a feed writes it: ISO 8601 (`2026-11-08T18:00:00Z`, with a fraction of a second or an offset, or a date alone) or RFC 822 (`Fri, 06 Nov 2026 22:15:59 +0000`, also the
 * names of the zones that RSS uses). The result is the seconds since 1970 (UTC). False if the text is none of them.
 */
bool ParseDate(const std::string& text, int64_t* epoch);

/** `2026-11-08T18:00:00Z` */
std::string FormatIso(int64_t epoch);

/** `2026-11-08 18.00`: the start of the name of a file (a colon is no good in a file name, and the names sort by date). */
std::string FileDatePrefix(int64_t epoch);

/** The text as a part of a file name: no slash and no control characters, at most `maxBytes`, not cut in the middle of a character. */
std::string FileNamePart(const std::string& text, size_t maxBytes);

/**
 * The address on the web of a Bluesky post. Bluesky names a post by an AT URI (`at://did:plc:abc/app.bsky.feed.post/3k44`), which no browser opens; the post is at
 * `https://bsky.app/profile/<handle or did>/post/<key>`. Any other address is returned as it is, and an AT URI that is not a post is returned empty.
 * @param handle the handle of the author (`alice.bsky.social`); if it is empty the DID of the URI is used (bsky.app takes both)
 */
std::string WebUrl(const std::string& url, const std::string& handle = std::string());

}	// namespace feedtext
}	// namespace robin
