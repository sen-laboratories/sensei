/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <vector>

#include "PaperAffiliations.h"

/**
 * @file PaperConfidence.h
 * @brief How sure Paperboy is about the relations that it makes (`SEN:REL:Confidence`, see sento's SenConfidence.h): the evidence for each, before any relation is written.
 *
 * | relation | evidence | weight |
 * |---|---|---|
 * | Authorship (person, paper) | the person was found by the ORCID iD that the paper gives for the author | identifier 0.97 |
 * | | the person was made from the entry of the paper | stated 0.90 |
 * | | an existing person of the same name was taken (the name alone) | name only 0.50 |
 * | Affiliation (person, institution) | ORCID lists the employment (the person keeps it) | stated 0.90 |
 * | | the paper names the institution for the author | stated 0.90 (both: 0.99) |
 * | Source Institution (paper, institution) | the paper names the institution for an author | stated 0.90 |
 * | | an employment with a start and an end covers the day of the paper | dated overlap 0.75 |
 * | | an employment that began before the paper and goes on | open overlap 0.55 |
 * | | an employment without dates | undated 0.35 |
 *
 * The evidence of an employment is divided among the institutions where the author held a position on that day (two employers: the paper may come from either, `Share`). The
 * institution of several authors is one relation: the evidence of the authors is combined, but the authors of one paper repeat each other (they share an address, copy a list), so each
 * further one counts half (`kRepeatedSource`). The institutions of a paper that two authors name are surer than one that a single author's ORCID record suggests.
 */
namespace paperboy {

/** how the person file for an author was found */
enum class PersonMatch {
	kCreated,			///< made from the entry of the paper
	kByIdentifier,		///< found by the ORCID iD
	kByNameOnly			///< an existing file of that name, nothing else known
};

/** how sure the Authorship between the person and the paper is */
double AuthorshipConfidence(PersonMatch match);

/** how sure the Affiliation of a person with the institution of the stay is (what the stay says: ORCID's employment, the paper's naming) */
double AffiliationConfidence(const Stay& stay);

/**
 * What one author's stay says for "the paper comes from this institution".
 * @param positions how many institutions the author has a stay at that the paper may come from (`ProducedPaper()`); the evidence of a position is shared among them
 * @return 0 if the stay is no evidence (the paper does not come from it)
 */
double ProductionEvidence(const Stay& stay, int positions);

/** the evidence of all the authors (one entry for each) combined: the confidence of the Source Institution relation */
double ProductionConfidence(const std::vector<double>& evidence);

}	// namespace paperboy
