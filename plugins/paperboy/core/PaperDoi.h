/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

/**
 * @file PaperDoi.h
 * @brief Finding and normalising DOIs (and arXiv identifiers, which have a DOI of their own) in text.
 */
namespace paperboy {

/**
 * @brief The bare, lower case DOI of a string: strips `https://doi.org/`, `doi:`, `urn:doi:` and trailing punctuation.
 * @return the DOI (10.1038/nature14539), or "" if the string is not a DOI
 */
std::string NormalizeDoi(const std::string& raw);

/**
 * @brief All DOIs in a text, normalised, in the order of their first appearance, without duplicates.
 *
 * Scans by hand instead of with std::regex: the text of a few pages is long enough to overflow the stack of a regex engine.
 * The pattern is that of the DOI Handbook, `10.\d{4,9}/[-._;()/:A-Z0-9]+`, case insensitive.
 */
std::vector<std::string> FindDois(const std::string& text);

/** @brief The arXiv identifiers in a text (`arXiv:1706.03762v5`, `arxiv.org/abs/1706.03762`, `arXiv:hep-th/9901001`), without the version. */
std::vector<std::string> FindArxivIds(const std::string& text);

/** @brief The DOI that DataCite registers for an arXiv identifier: 10.48550/arxiv.1706.03762 */
std::string ArxivDoi(const std::string& arxivId);

}	// namespace paperboy
