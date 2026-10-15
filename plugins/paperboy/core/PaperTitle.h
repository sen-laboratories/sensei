/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

namespace paperboy {

/** @brief The title in comparable form: lower case (ASCII), punctuation and blanks as single blanks. */
std::string NormalizeTitle(const std::string& title);

/**
 * @brief How alike two titles are, 0 to 1 (the share of words that both have, of all words that either has).
 * Used to accept a hit of the title search only if it is the paper that was asked for.
 */
double TitleSimilarity(const std::string& a, const std::string& b);

/**
 * @brief Whether a title from the metadata of a file can be the title of a paper. Word processors and PDF writers fill the field with
 * the name of the file ("Microsoft Word - draft.docx"), with "untitled" or with nothing.
 */
bool LooksLikeTitle(const std::string& title);

}	// namespace paperboy
