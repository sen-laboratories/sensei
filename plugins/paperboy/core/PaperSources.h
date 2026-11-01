/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

#include "PaperModel.h"

/**
 * @file PaperSources.h
 * @brief The other scientific sources, for papers that OpenAlex does not know: arXiv, Semantic Scholar and DBLP.
 *
 * All three answer without a key or a login (Semantic Scholar shares a small pool among all clients without a key, and then answers 429; DBLP
 * can put a challenge for bots in front of its interface, which is no answer for a program). No network and no BeAPI here: the functions make the addresses
 * and read the bodies of the answers into the model of paperboy (the field `service` of the paper says where it came from).
 */
namespace paperboy {

namespace service {
inline constexpr char kOpenAlex[]        = "openalex";
inline constexpr char kArxiv[]           = "arxiv";
inline constexpr char kSemanticScholar[] = "semanticscholar";
inline constexpr char kDblp[]            = "dblp";
}

/** @brief `https://export.arxiv.org/api/query?id_list=<id>`, the id without version (`1706.03762`, `hep-th/9901001`) */
std::string ArxivIdUrl(const std::string& arxivId);

/** @brief The papers of arXiv whose title has these words (the five best). */
std::string ArxivSearchUrl(const std::string& title);

/** @brief The papers of Semantic Scholar for a title (the five best, with abstract, open access PDF, authors, venue, external ids and citation count). */
std::string SemanticScholarSearchUrl(const std::string& title);

/** @brief The papers of DBLP for a title (the five best, as JSON). */
std::string DblpSearchUrl(const std::string& title);

/**
 * @brief The entries of an Atom feed of the arXiv API: title, authors, abstract, date, categories (keywords), the DOI (that of the journal if the authors gave
 * one, else the one that DataCite registers for the preprint), the journal reference, and the PDF as the open access location.
 * @return false if the body is not a feed of the arXiv API
 */
bool ParseArxivFeed(const std::string& xml, std::vector<Paper>* papers);

/** @brief The papers of a search of Semantic Scholar (`/graph/v1/paper/search`); false if the body is not its JSON (an error answer has no `data`). */
bool ParseSemanticScholarSearch(const std::string& json, std::vector<Paper>* papers);

/** @brief The hits of a search of DBLP (`/search/publ/api?format=json`); false if the body is not its JSON (a page that asks for proof of being no bot is HTML). */
bool ParseDblpSearch(const std::string& json, std::vector<Paper>* papers);

}	// namespace paperboy
