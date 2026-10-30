/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>
#include <String.h>

#include <sen/SenChoice.h>

/**
 * How good is an answer of OpenLibrary for the book that was asked for, from 0 to 1, out of four things:
 *   - the rank that OpenLibrary gives (its own relevance, 35%),
 *   - how close the title is to the one asked for (30%: the same is 1, one contains the other 0.6),
 *   - how complete the entry is (20%: a cover, ISBN, authors, publisher, pages, year and language),
 *   - how well known the book is (15%: the number of its editions and of the readers who have it on their shelves).
 * The weights are a guess that is easy to change; the window only sorts by the result.
 * @param doc an answer of the search (a message of the fields of OpenLibrary)
 * @param position its place in the answer of the service, 0 is the first
 * @param count how many answers there are
 * @param wanted the title that was asked for, empty if the search was for something else
 */
float ScoreBook(const BMessage& doc, int32 position, int32 count, const BString& wanted);

/** An answer of OpenLibrary as it is shown in the list of sen::ChooseCandidate(): title, authors and year, ISBN and pages, the source. */
sen::Candidate DescribeBook(const BMessage& doc, float score);

/** Whether the title of an answer is the title that was asked for, or one holds the other ("The Pragmatic Programmer: from journeyman to master" for "The Pragmatic Programmer"); true if nothing was asked. */
bool TitleMatches(const BMessage& doc, const BString& wanted);
