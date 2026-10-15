/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

#include "PaperModel.h"

namespace paperboy {

/** The pattern that names a file when renaming is switched on. Tokens: {year} {first_author} {title} {doi}. */
inline constexpr char kDefaultNamePattern[] = "{year}_{first_author}_{title}";

/** @brief The family name of an author as OpenAlex writes him: "Yann LeCun" -> "LeCun", "Doe, John" -> "Doe". */
std::string FamilyName(const std::string& displayName);

/**
 * @brief Makes a string fit for a file name: blanks become `_`, characters that file managers and shells dislike are dropped,
 * repeated `_` collapse, the result is cut at a word boundary after at most @p maxBytes (never inside a UTF-8 sequence).
 */
std::string SanitizeForFileName(const std::string& text, size_t maxBytes = 80);

/**
 * @brief The file name for a paper from a pattern: `2015_LeCun_Deep_learning.pdf`.
 * @param extension with the dot (".pdf") or ""
 * @return "" if the paper has too little data for a useful name (no title)
 */
std::string BuildFileName(const std::string& pattern, const Paper& paper, const std::string& extension);

}	// namespace paperboy
