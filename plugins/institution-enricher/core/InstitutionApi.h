/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file InstitutionApi.h
 * @brief The Research Organization Registry (ROR, api.ror.org, version 2) and the institutions of OpenAlex: the addresses to ask, and what the answers say.
 *
 * ROR names the institution (names, types, place, links, the institutions that it is related to); OpenAlex has what is counted (works, citations) and the topics. Only OpenAlex
 * links the two (its `ror` field), so the registry is asked first and OpenAlex by the registry's id.
 */

#include <string>
#include <vector>

#include "InstitutionModel.h"

namespace institution {

/** how OpenAlex is asked: an address for its polite pool, and the free key (without one all requests of an address share a small daily budget) */
struct OpenAlexAccess {
	std::string mailto;
	std::string apiKey;
};

std::string UrlEncode(const std::string& text);

/** `https://ror.org/04d836q62`, `ror.org/04d836q62` or `04d836q62` is `04d836q62`; anything else that is not an id is empty */
std::string BareRorId(const std::string& text);

/** `https://api.ror.org/v2/organizations/<id>` */
std::string RorRecordUrl(const std::string& rorId);
/** the affiliation search: the registry scores the answers and says which one it takes (`chosen`) */
std::string RorAffiliationUrl(const std::string& name);
/** `https://api.openalex.org/institutions?filter=ror:...`: the institution of that registry id */
std::string OpenAlexInstitutionUrl(const std::string& rorId, const OpenAlexAccess& access);

/** the record of a registry answer for one organization (the answer of RorRecordUrl, or `organization` of one answer of a search) */
bool ParseRorRecord(const std::string& json, Record* record);
/** the answers of a search, the best first (the affiliation search has the scores; a plain `query` search has not: score 0) */
std::vector<Hit> ParseRorSearch(const std::string& json);
/** adds what OpenAlex says to the record (the works, the citations, the topics, its id; what the registry did not say: the type, the country, the homepage). False if the answer has no institution of that registry id. */
bool MergeOpenAlex(const std::string& json, Record* record);

/** a name for comparing: lower case, no accents of the common languages, no punctuation, single blanks */
std::string NormalizeName(const std::string& name);
/** whether the name is the name of the record (its display name, or one of the other names), as written or as the same words */
bool NameMatches(const Record& record, const std::string& name);

/** the registry's type (`education`) as the text of the ontology (`Education`) */
std::string TypeText(const std::string& rorType);

}	// namespace institution
