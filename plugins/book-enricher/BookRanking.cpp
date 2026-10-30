/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "BookRanking.h"

#include <math.h>

#include <algorithm>

// ---- how good is an answer

/** a number of the answer of the service (the JSON parser gives doubles, but be safe), 0 if it has none */
static double
NumberOf(const BMessage& doc, const char* name)
{
    double number;
    if (doc.FindDouble(name, &number) == B_OK)
        return number;
    int64 whole;
    if (doc.FindInt64(name, &whole) == B_OK)
        return (double) whole;
    int32 small;
    if (doc.FindInt32(name, &small) == B_OK)
        return (double) small;
    return 0;
}

/** the first strings of a list of the answer (a message with the keys "0", "1"...), joined; `more` tells that there were more */
static BString
FirstStrings(const BMessage& doc, const char* name, int32 max, const char* separator, bool* more = NULL)
{
    BString joined;
    BMessage list;
    if (more != NULL)
        *more = false;
    if (doc.FindMessage(name, &list) != B_OK)
        return joined;

    int32 taken = 0;
    for (int32 i = 0; i < list.CountNames(B_STRING_TYPE); i++) {
        BString key;
        key << i;
        const char* value = NULL;
        if (list.FindString(key.String(), &value) != B_OK || value == NULL || value[0] == '\0')
            continue;
        if (taken == max) {
            if (more != NULL)
                *more = true;
            break;
        }
        if (taken > 0)
            joined << separator;
        joined << value;
        taken++;
    }
    return joined;
}

/** lower case letters and digits only: "The Pragmatic Programmer: from journeyman..." is compared with "the pragmatic programmer" */
static BString
Normalized(const char* text)
{
    BString result;
    for (const char* c = text; c != NULL && *c != '\0'; c++) {
        unsigned char character = (unsigned char) *c;
        if (character >= 'A' && character <= 'Z')
            result << (char) (character - 'A' + 'a');
        else if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9'))
            result << (char) character;
        else if (character >= 0x80)
            result << (char) character;     // (letters outside of ASCII stay as they are)
    }
    return result;
}

/** how well known: 0 (never heard of) to 1 (a count of `full` or more), on a logarithmic scale: ten editions are not ten times better than one */
static float
Known(double count, double full)
{
    if (count <= 0)
        return 0;
    return (float) std::min(1.0, log1p(count) / log1p(full));
}

/**
 * How good an answer of OpenLibrary is for the book that was asked for, from 0 to 1, out of four things:
 *   - the rank that OpenLibrary gives (its own relevance, 35%),
 *   - how close the title is to the one asked for (30%: the same is 1, one contains the other 0.6),
 *   - how complete the entry is (20%: a cover, ISBN, authors, publisher, pages, year and language),
 *   - how well known the book is (15%: the number of its editions and of the readers who have it on their shelves).
 * The weights are a guess that is easy to change; the window only sorts by the result.
 */
float
ScoreBook(const BMessage& doc, int32 position, int32 count, const BString& wanted)
{
    const float rank = count > 1 ? 1.0f - (float) position / (float) count : 1.0f;

    float title = 0.5f;     // (nothing to compare: neither good nor bad)
    const BString asked = Normalized(wanted.String());
    if (!asked.IsEmpty()) {
        const BString found = Normalized(doc.GetString("title", ""));
        if (found == asked)
            title = 1.0f;
        else if (!found.IsEmpty() && (found.FindFirst(asked) >= 0 || asked.FindFirst(found) >= 0))
            title = 0.6f;
        else
            title = 0.0f;
    }

    const char* const fields[] = {"cover_i", "isbn", "author_name", "publisher", "number_of_pages_median", "first_publish_year", "language"};
    int present = 0;
    for (const char* field : fields) {
        if (doc.HasData(field, B_ANY_TYPE))
            present++;
    }
    const float complete = (float) present / (float) (sizeof(fields) / sizeof(fields[0]));

    const float known = std::max(Known(NumberOf(doc, "edition_count"), 50), Known(NumberOf(doc, "readinglog_count"), 3000));

    return rank * 0.35f + title * 0.30f + complete * 0.20f + known * 0.15f;
}

bool
TitleMatches(const BMessage& doc, const BString& wanted)
{
    const BString asked = Normalized(wanted.String());
    if (asked.IsEmpty())
        return true;
    const BString found = Normalized(doc.GetString("title", ""));
    return !found.IsEmpty() && (found.FindFirst(asked) >= 0 || asked.FindFirst(found) >= 0);
}

/** an answer of OpenLibrary as it is shown in the list */
sen::Candidate
DescribeBook(const BMessage& doc, float score)
{
    static const char* const kDot = " \xc2\xb7 ";    // (a middle dot)

    sen::Candidate candidate;
    candidate.title = doc.GetString("title", "(no title)");
    candidate.source = "OpenLibrary";
    candidate.score = score;

    bool more = false;
    BString authors = FirstStrings(doc, "author_name", 3, ", ", &more);
    if (more)
        authors << " et al.";
    candidate.subtitle = authors;
    const double year = NumberOf(doc, "first_publish_year");
    if (year > 0) {
        if (!candidate.subtitle.IsEmpty())
            candidate.subtitle << kDot;
        candidate.subtitle << (int) year;
    }

    BString detail;
    BString isbn = FirstStrings(doc, "isbn", 1, "");
    if (!isbn.IsEmpty())
        detail << "ISBN " << isbn;
    const double pages = NumberOf(doc, "number_of_pages_median");
    if (pages > 0) {
        if (!detail.IsEmpty())
            detail << kDot;
        detail << (int) pages << " pages";
    }
    BString language = FirstStrings(doc, "language", 1, "");
    if (!language.IsEmpty()) {
        if (!detail.IsEmpty())
            detail << kDot;
        detail << language;
    }
    const double editions = NumberOf(doc, "edition_count");
    if (editions > 1) {
        if (!detail.IsEmpty())
            detail << kDot;
        detail << (int) editions << " editions";
    }
    candidate.detail = detail;
    return candidate;
}
