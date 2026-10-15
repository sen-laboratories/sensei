/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

/**
 * @file PaperApi.h
 * @brief The addresses of the two services (OpenAlex and Unpaywall), built from what is asked.
 *
 * Both have a "polite pool" for clients that name an address: OpenAlex takes it as `mailto`, Unpaywall requires it as `email`.
 */
namespace paperboy {

/** @brief Percent-encodes everything but the unreserved characters and those in @p keep. */
std::string UrlEncode(const std::string& text, const char* keep = "");

/**
 * What identifies the client to OpenAlex: the address for the polite pool and, if there is one, the key. (Without a key all clients of an
 * address share a small daily budget; the key is free.)
 */
struct OpenAlexAccess {
	std::string mailto;
	std::string apiKey;
};

/** @brief `https://api.openalex.org/works/https://doi.org/<doi>` */
std::string OpenAlexWorkUrl(const std::string& doi, const OpenAlexAccess& access);

/** @brief Title search: the five best hits with their id, DOI, title and year (the title is cleaned of the characters that OpenAlex reads as syntax). */
std::string OpenAlexSearchUrl(const std::string& title, const OpenAlexAccess& access);

/** @brief The works with these ids (`W123`) in one request, with their id, DOI, title and year; at most kBatchSize ids. */
std::string OpenAlexBatchUrl(const std::vector<std::string>& ids, const OpenAlexAccess& access);

/** OpenAlex takes at most 100 values in an OR filter; 50 keep the answer small. */
inline constexpr size_t kBatchSize = 50;

/** @brief `https://api.unpaywall.org/v2/<doi>?email=<email>` */
std::string UnpaywallUrl(const std::string& doi, const std::string& email);

/** @brief The user agent of paperboy; with an address it is the polite form `paperboy/0.1 (mailto:...)`. */
std::string UserAgent(const std::string& email);

}	// namespace paperboy
