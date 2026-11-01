/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

/**
 * @file PaperModel.h
 * @brief What paperboy knows about a paper: plain C++ types, filled from the answers of OpenAlex and Unpaywall.
 *
 * The code in `core/` uses the standard library only (and nlohmann::json), so that it is tested on any machine.
 */
namespace paperboy {

struct Author {
	std::string name;
	std::string orcid;							///< bare ORCID iD (0000-0002-1992-2684), not the URL
	std::vector<std::string> affiliations;
};

/** A place where the paper can be read: the landing page and/or a direct link to the PDF. */
struct Location {
	std::string landingUrl;
	std::string pdfUrl;
	std::string version;						///< publishedVersion, acceptedVersion, submittedVersion
	std::string license;
	std::string hostType;						///< publisher or repository
	std::string source;							///< unpaywall or openalex
	bool isBest = false;

	/** The address to open: the PDF if there is one, else the landing page. */
	const std::string& Url() const { return pdfUrl.empty() ? landingUrl : pdfUrl; }
};

/** A work that a paper cites. */
struct Reference {
	std::string openAlexId;						///< W2919115771
	std::string doi;
	std::string title;
	int year = 0;
};

struct Paper {
	std::string doi;							///< bare, lower case: 10.1038/nature14539
	std::string openAlexId;						///< W2919115771
	std::string arxivId;						///< 1706.03762, if the paper is on arXiv
	std::string service;						///< where the data comes from: openalex, arxiv, semanticscholar, dblp
	std::string title;
	std::string type;							///< OpenAlex work type: article, preprint, book-chapter, ...
	std::string date;							///< publication date as YYYY-MM-DD (or YYYY)
	int year = 0;
	std::string language;
	std::string publisher;
	std::string venue;							///< journal, conference or repository
	std::string abstract;
	std::vector<std::string> keywords;
	std::vector<Author> authors;

	bool isOa = false;
	std::string oaStatus;						///< gold, green, hybrid, bronze, diamond, closed
	std::vector<Location> locations;			///< the best first
	int citedByCount = -1;						///< -1: not known
	std::vector<std::string> referencedWorks;	///< OpenAlex ids

	bool fromOpenAlex = false;
	bool fromUnpaywall = false;

	/** The best location, or NULL for a closed paper. */
	const Location* BestLocation() const { return locations.empty() ? nullptr : &locations.front(); }
};

}	// namespace paperboy
