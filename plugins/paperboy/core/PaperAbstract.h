/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace paperboy {

/** A word of an inverted index and the positions where it stands in the text. */
using InvertedWord = std::pair<std::string, std::vector<int>>;

/**
 * @brief Rebuilds the text of an abstract from the inverted index that OpenAlex delivers (`abstract_inverted_index`).
 *
 * Every word is put at each of its positions; gaps are skipped, so a damaged index still gives readable text.
 * Kept apart from the JSON code so that it is tested alone.
 * @return the abstract with single blanks between the words, "" for an empty index
 */
std::string ReconstructAbstract(const std::vector<InvertedWord>& index);

}	// namespace paperboy
