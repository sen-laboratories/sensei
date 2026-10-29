/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

namespace paperboy {

/**
 * @brief Where a paper with this DOI is kept, for people: a preprint server or a repository ("arXiv", "Zenodo"), or "Publisher" for any other DOI.
 *
 * A preprint and the paper as it was published have the same title and nearly the same authors: the place that the DOI comes from is what tells them apart
 * in a list. It is the registrant code of the DOI (the part before the first slash), compared as lower case. Empty for no DOI.
 */
std::string SourceOfDoi(const std::string& doi);

/**
 * @brief How good a hit of the title search is for the title that was searched, from 0 to 1.
 *
 * Mostly how much it is like the title (85%), then the rank that the service gave it (10%: the first hit gets all of it), and that it says its year (5%). Only hits that are
 * at least 85% like the title are scored, so their similarities are close: a small weight for the rank decides only between equal ones, and an exact match beats a better ranked
 * near match.
 * @param similarity 0 to 1, see TitleSimilarity()
 * @param rank 0 for the first hit of the service
 * @param count how many hits the service gave
 */
double ScoreHit(double similarity, int rank, int count, int year);

}	// namespace paperboy
