/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// The core of the institution enricher against real answers of ROR (version 2) and OpenAlex (tests/fixtures).

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#include "InstitutionApi.h"

#ifndef FIXTURES
#error "FIXTURES: the folder of the answers"
#endif

using namespace institution;

static int gChecks = 0, gFailed = 0;

#define CHECK(condition) do { gChecks++; if (!(condition)) { gFailed++; std::cout << "FAILED " << __LINE__ << ": " #condition "\n"; } } while (0)

static std::string
Fixture(const std::string& name)
{
	std::ifstream in(std::string(FIXTURES) + "/" + name);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

static bool
Has(const std::vector<std::string>& list, const std::string& value)
{
	return std::find(list.begin(), list.end(), value) != list.end();
}

int
main()
{
	// the ids
	CHECK(BareRorId("https://ror.org/04d836q62") == "04d836q62");
	CHECK(BareRorId("  ror.org/04d836q62 ") == "04d836q62");
	CHECK(BareRorId("04d836q62") == "04d836q62");
	CHECK(BareRorId("https://openalex.org/I145847075").empty());
	CHECK(BareRorId("TU Wien").empty());
	CHECK(BareRorId("14d836q62").empty());

	// the addresses
	CHECK(RorRecordUrl("04d836q62") == "https://api.ror.org/v2/organizations/04d836q62");
	CHECK(RorAffiliationUrl("Université de Paris") == "https://api.ror.org/v2/organizations?affiliation=Universit%C3%A9%20de%20Paris");
	OpenAlexAccess none;
	CHECK(OpenAlexInstitutionUrl("04d836q62", none) == "https://api.openalex.org/institutions?filter=ror:https%3A%2F%2Fror.org%2F04d836q62");
	OpenAlexAccess access;
	access.mailto = "a@b.org";
	access.apiKey = "k3y";
	CHECK(OpenAlexInstitutionUrl("04d836q62", access).find("&mailto=a%40b.org&api_key=k3y") != std::string::npos);

	// a record of the registry
	Record tu;
	CHECK(ParseRorRecord(Fixture("ror-tuwien.json"), &tu));
	CHECK(tu.rorId == "04d836q62");
	CHECK(tu.name == "TU Wien");
	CHECK(tu.types.size() == 2 && tu.types[0] == "Education" && tu.types[1] == "Funder");
	CHECK(tu.country == "AT" && tu.locality == "Vienna");
	CHECK(tu.homepage == "https://www.tuwien.at");
	CHECK(!tu.otherNames.empty() && tu.otherNames[0] == "TUW");		// (the acronym first)
	CHECK(Has(tu.otherNames, "Technische Universit\xC3\xA4t Wien") && Has(tu.otherNames, "Vienna University of Technology"));
	CHECK(!Has(tu.otherNames, "TU Wien"));		// (the name is not one of the other names)
	CHECK(Has(tu.sameAs, "https://ror.org/04d836q62"));
	CHECK(Has(tu.sameAs, "https://www.wikidata.org/entity/Q689400"));
	CHECK(Has(tu.sameAs, "https://en.wikipedia.org/wiki/TU_Wien"));
	CHECK(Has(tu.sameAs, "https://isni.org/isni/0000000419370669"));
	CHECK(Has(tu.sameAs, "https://www.grid.ac/institutes/grid.5329.d"));
	CHECK(tu.related.size() == 3 && tu.related[0].kind == "child" && tu.related[0].rorId == "01cbw5x35" && tu.related[2].kind == "related");
	CHECK(tu.worksCount == -1 && tu.topics.empty());
	CHECK(tu.status == "active");
	Record bad;
	CHECK(!ParseRorRecord("{\"id\": \"nothing\"}", &bad));
	CHECK(!ParseRorRecord("not json", &bad));

	// the affiliation search: the registry says which one it takes
	std::vector<Hit> hits = ParseRorSearch(Fixture("ror-affiliation.json"));
	CHECK(hits.size() == 10);
	CHECK(!hits.empty() && hits[0].record.rorId == "04d836q62" && hits[0].chosen && hits[0].score == 1.0);
	int chosen = 0;
	for (const Hit& hit : hits)
		chosen += hit.chosen ? 1 : 0;
	CHECK(chosen == 1);
	for (size_t i = 1; i < hits.size(); i++)
		CHECK(hits[i - 1].score >= hits[i].score);

	// the plain search has the organizations themselves, with no score
	hits = ParseRorSearch(Fixture("ror-query.json"));
	CHECK(!hits.empty() && hits[0].score == 0 && !hits[0].chosen && !hits[0].record.rorId.empty());
	CHECK(ParseRorSearch("{}").empty() && ParseRorSearch("nonsense").empty());

	// OpenAlex adds what is counted and the topics
	Record merged = tu;
	CHECK(MergeOpenAlex(Fixture("openalex-tuwien.json"), &merged));
	CHECK(merged.openAlexId == "I145847075");
	CHECK(merged.worksCount > 1000 && merged.citedByCount > merged.worksCount);
	CHECK(Has(merged.sameAs, "https://openalex.org/I145847075"));
	CHECK(merged.types.size() == 2);		// (the registry's types stay)
	CHECK(!merged.topics.empty() && !merged.topics[0].name.empty() && merged.topics[0].openAlexId[0] == 'T');
	for (size_t i = 1; i < merged.topics.size(); i++)
		CHECK(merged.topics[i - 1].worksCount >= merged.topics[i].worksCount);
	// the Wikidata item is one link, as the registry says it
	int wikidata = 0;
	for (const std::string& link : merged.sameAs)
		wikidata += link.find("wikidata.org") != std::string::npos ? 1 : 0;
	CHECK(wikidata == 1);
	// an institution of another registry id is not this one
	Record other = tu;
	other.rorId = "01cbw5x35";
	CHECK(!MergeOpenAlex(Fixture("openalex-tuwien.json"), &other));
	CHECK(other.worksCount == -1);

	// names
	CHECK(NormalizeName("Technische Universit\xC3\xA4t Wien") == "technische universitat wien");
	CHECK(NormalizeName("  TU   Wien, (Vienna) ") == "tu wien vienna");
	CHECK(NormalizeName("Stra\xC3\x9F" "e") == "strasse");
	CHECK(NameMatches(tu, "TU Wien") && NameMatches(tu, "tu-wien") && NameMatches(tu, "Technische Universitaet Wien") == false);
	CHECK(NameMatches(tu, "Technische Universit\xC3\xA4t Wien") && NameMatches(tu, "vienna university of technology") && NameMatches(tu, "TUW"));
	CHECK(!NameMatches(tu, "University of Vienna") && !NameMatches(tu, ""));
	CHECK(TypeText("education") == "Education" && TypeText("").empty());

	std::cout << gChecks << " checks, " << gFailed << " failed\n";
	return gFailed == 0 ? 0 : 1;
}
