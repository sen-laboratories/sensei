/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

#include "PaperModel.h"
#include "PaperPeople.h"

/**
 * @file PaperAffiliations.h
 * @brief Which institutions an author is affiliated with, from what the paper names and what ORCID lists: the plan, before any file is made.
 *
 * Two sources say something different. The paper names the institutions of the author **at the time of the paper** (OpenAlex: with the ROR id, the country and the type), but gives no
 * dates and no role. ORCID lists the employments that the person keeps, with the dates, the role and the department, and names the institution by a register's id (ROR where it can).
 * The plan joins them by the ROR id:
 *
 * - an institution of the paper that ORCID lists an employment at: one stay for each such employment, with its dates and its role (a promotion is a second stay);
 * - an institution of the paper that ORCID does not know: one stay without dates;
 * - an employment at ORCID that goes on (no end) at an institution that the paper does not name: one stay (where the person is now);
 * - an employment of the past at an institution that the paper does not name is left out: a career would make a file of every institution the person ever worked at.
 *
 * Only an institution with a ROR id is an institution: nobody is guessed from a name.
 */
namespace paperboy {

/** the person has been at (or is at) the institution, from `since` to `until` (empty: not said, or not over), as what */
struct Stay {
	std::string rorId;							///< bare
	std::string name;
	std::string country;						///< ISO 3166-1 alpha-2, may be empty
	std::string city;							///< may be empty
	std::string type;							///< `Education`... as the ontology says it, empty if unknown
	std::string since;							///< YYYY, YYYY-MM or YYYY-MM-DD, or empty
	std::string until;
	std::string role;
	std::string note;							///< the department
	bool fromEmployment = false;				///< the dates and the role are ORCID's
};

/** @brief The stays of one author (see above); the institutions of the paper first, in the order of the paper. */
std::vector<Stay> PlanAffiliations(const std::vector<Affiliation>& named, const std::vector<Employment>& employments);

/** @brief The type of OpenAlex (`education`, lower case) as the text of the ontology (`Education`); empty for none. */
std::string InstitutionTypeText(const std::string& openAlexType);

}	// namespace paperboy
