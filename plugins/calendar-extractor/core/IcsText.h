/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

#include "IcsTime.h"

/**
 * @file IcsText.h
 * @brief The text of an event: its description as Markdown, the links in it and the online meeting among them, the name of the file. Plain C++.
 */

namespace ics {

/** Whether a text has HTML in it (tags that a description holds: `<br>`, `<a href=...>`, `<p>`...). */
bool LooksLikeHtml(const std::string& text);

/** HTML as Markdown: links, bold, italic, lists, headings and line breaks; the rest of the tags are left out, the entities are decoded. */
std::string HtmlToMarkdown(const std::string& html);

/**
 * The description as Markdown. @p html is the alternative text in HTML that a file may have besides (X-ALT-DESC) and is taken when there is one; else @p text is taken, as HTML if it holds
 * tags, else as plain text: its paragraphs and line breaks are kept, a link becomes `<https://...>`, and the characters that Markdown would take for formatting are escaped.
 */
std::string DescriptionToMarkdown(const std::string& text, const std::string& html);

/** The links (http, https) in a text: of plain text, of Markdown (also those in angle brackets and the target of `[text](url)`) or of HTML (`href`); in order, each once, without the dot or bracket that ends a sentence. */
std::vector<std::string> FindUrls(const std::string& text);

/**
 * Whether a link is that of an online meeting of a known service, and which: Zoom, Google Meet, Microsoft Teams, Webex, Jitsi, GoToMeeting, Whereby, Skype (the titles of the values of
 * Meeting Type in the ontology).
 */
bool ClassifyMeetingUrl(const std::string& url, std::string* service);

/** The first lines of a text without the marks of Markdown, in one line, for a column: at most @p limit characters, cut at a word. */
std::string PlainSummary(const std::string& markdown, size_t limit);

/**
 * The name of the file for an event: its start in the local zone, then its title: `2026-11-12_0930_Planning`, a day has no time: `2026-12-24_Christmas`. A name that has no title is the date
 * and `Event`. Blanks become `-`, what a file name cannot hold (`/`, control characters) is left out, and a long title is cut at a word. No extension.
 */
std::string EventBaseName(const DateTime& localStart, bool hasTime, const std::string& title);

}	// namespace ics
