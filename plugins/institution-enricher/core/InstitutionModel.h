/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace institution {

/** a topic that OpenAlex gives an institution, with the number of its works about it */
struct Topic {
	std::string openAlexId;		///< `T12188`
	std::string name;
	int64_t		worksCount = 0;
};

/** an institution that the registry relates to this one: kind is `parent`, `child`, `related`, `predecessor` or `successor` (as the registry says it of the OTHER one) */
struct Related {
	std::string rorId;			///< bare
	std::string name;
	std::string kind;
};

/** what the registries know of an institution */
struct Record {
	std::string					rorId;			///< the bare id of the Research Organization Registry (`04d836q62`), the key of the institution
	std::string					name;			///< the name that the registry shows
	std::vector<std::string>	otherNames;		///< the acronyms first, then the aliases and the names in other languages
	std::vector<std::string>	types;			///< `Education`, `Funder`... (the texts of the type of the ontology)
	std::string					homepage;
	std::string					locality;		///< the city
	std::string					country;		///< ISO 3166-1 alpha-2
	std::vector<std::string>	sameAs;			///< IRIs: the registry, Wikidata, Wikipedia, OpenAlex, ISNI, GRID...
	std::string					openAlexId;		///< `I145847075`
	int64_t						worksCount = -1;	///< -1: not known (OpenAlex was not asked, or does not know it)
	int64_t						citedByCount = -1;
	std::vector<Topic>			topics;			///< the most frequent first
	std::vector<Related>		related;
	std::string					status;			///< `active`, `inactive`, `withdrawn`

	bool HasRor() const { return !rorId.empty(); }
};

/** an answer of the registry to a search: how well it fits (0..1) and whether the registry says that it is the one */
struct Hit {
	Record	record;
	double	score = 0;
	bool	chosen = false;
};

}	// namespace institution
