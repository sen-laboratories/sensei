/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

#include "PaperModel.h"

/**
 * @file PaperJson.h
 * @brief Reads the answers of OpenAlex and Unpaywall into the model of paperboy.
 *
 * No network and no BeAPI here: the functions get the body of an answer and return what is in it.
 */
namespace paperboy {

/** @brief `https://openalex.org/W2919115771` -> `W2919115771` (anything else stays as it is) */
std::string ShortOpenAlexId(const std::string& id);

/**
 * @brief Reads a work of OpenAlex (`/works/<id>`): bibliographic data, authors with ORCID and affiliations, keywords (score >= 0.3),
 * the abstract, the open access locations and the ids of the cited works.
 * @return false if the body is not the JSON of a work
 */
bool ParseOpenAlexWork(const std::string& json, Paper* paper);

/**
 * @brief Reads an answer of Unpaywall and puts its open access data into the paper, replacing the locations that came from OpenAlex:
 * Unpaywall is the authority for the status and the best location.
 * @return false if the body is not the JSON of a DOI record
 */
bool ParseUnpaywall(const std::string& json, Paper* paper);

/** @brief The works of a list answer of OpenAlex (`/works?filter=...` or `?search=...`, with the fields id, doi, title, publication_year). */
std::vector<Reference> ParseOpenAlexList(const std::string& json);

}	// namespace paperboy
