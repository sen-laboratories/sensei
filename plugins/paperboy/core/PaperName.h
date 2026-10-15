/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

#include "PaperModel.h"

namespace paperboy {

/** The pattern that names a file when it is renamed. Tokens: {year} {first_author} {title} {doi}. The default is the title alone. */
inline constexpr char kDefaultNamePattern[] = "{title}";

/** @brief The family name of an author as OpenAlex writes him: "Yann LeCun" -> "LeCun", "Doe, John" -> "Doe". */
std::string FamilyName(const std::string& displayName);

/**
 * @brief Makes a string fit for a file name. What Haiku, other systems and shells do not accept or dislike goes: control characters,
 * `\ * ? " < > |`; a `/` becomes `-` and a `:` becomes " - " (`_` with @p underscores). Blanks collapse to one; with @p underscores they
 * become `_`. A leading `.` or `-` is dropped, as is a dot, dash or blank at the end. The result is cut at a word boundary after at most
 * @p maxBytes (never inside a UTF-8 sequence).
 */
std::string SanitizeForFileName(const std::string& text, size_t maxBytes = 100, bool underscores = false);

/**
 * @brief The file name for a paper from a pattern: `Deep learning.pdf` for `{title}`, `2015_LeCun_Deep_learning.pdf` for
 * `{year}_{first_author}_{title}` with @p underscores.
 * @param extension with the dot (".pdf") or ""
 * @return "" if the paper has too little data for a useful name (no title)
 */
std::string BuildFileName(const std::string& pattern, const Paper& paper, const std::string& extension, bool underscores = false);

}	// namespace paperboy
