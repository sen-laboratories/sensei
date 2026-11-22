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

/** The pattern that names the file of an event unless the settings say otherwise: the day, the time if the event has one, the title. */
inline constexpr char kDefaultNamePattern[] = "{date}_{time}_{title}";

/**
 * Whether a pattern can name a file: it has text, only the tokens {date} (2026-11-12), {time} (0930, empty for a day), {year}, {month}, {day}, {hour}, {minute} and {title}, and no `/`. @p reason gets
 * what is wrong.
 */
bool IsValidNamePattern(const std::string& pattern, std::string* reason = nullptr);

/**
 * The name of the file for an event from a pattern (see kDefaultNamePattern; start in the local zone, a day has no time: `2026-12-24_Christmas`). Blanks in the title become `-`, what a file name cannot
 * hold (`/`, control characters) is left out, a long title is cut at a word; a token that is empty (the time of a day) takes one of the separators around it with it. A title that is empty is `Event`; a bad
 * pattern is the default one. No extension.
 */
std::string BuildEventName(const std::string& pattern, const DateTime& localStart, bool hasTime, const std::string& title);

/**
 * A recurrence rule in words: `every year`, `every 2 weeks on Monday and Wednesday, 10 times`, `every month on the second Tuesday`, `every year on 5 May, until 2030-12-31`. Empty if the rule is none.
 * The rule itself stays in the attribute of the rule; this is what people read.
 */
std::string DescribeRule(const Rule& rule);
/**
 * The recurrence rule that the words of the field Repeats say (the words that DescribeRule makes, and the common ones that a person chooses or types): every day, every weekday, every week,
 * every month, every year, every 2 weeks. False for words with more in them (on Monday, until 2030): those are not a rule that can be worked out from the words.
 */
bool RuleFromWords(const std::string& text, Rule* rule);


/** The name of the file for an event after the default pattern: `2026-11-12_0930_Planning`. */
std::string EventBaseName(const DateTime& localStart, bool hasTime, const std::string& title);

}	// namespace ics
