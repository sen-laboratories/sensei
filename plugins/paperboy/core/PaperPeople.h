/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

/**
 * @file PaperPeople.h
 * @brief What is known about the author of a paper, from ORCID and Wikidata (with the photo from Wikimedia Commons).
 *
 * An author is identified by the ORCID iD that OpenAlex gives for the authorship: ORCID says who the person is (name, biography written by the person,
 * homepage, the current employer), Wikidata is asked for the entry with that ORCID (property P496: an exact match, no guessing by name) and, for a
 * notable person, has a photo at Wikimedia Commons with the license and the name of the photographer. None of the three needs a key or a login.
 *
 * No network and no BeAPI here: the functions make the addresses and read the bodies of the answers.
 */
namespace paperboy {

struct PersonData {
	std::string name;
	std::string givenName;
	std::string familyName;
	std::string orcid;							///< bare: 0000-0002-9322-3515
	std::string wikidataId;						///< Q3572699
	std::string biography;						///< the one that the person wrote at ORCID, else the short description of Wikidata
	std::string jobTitle;						///< "Professor, University of Montreal": the current employment at ORCID
	std::string birthDate;						///< YYYY-MM-DD, or YYYY if Wikidata knows no more
	std::string homepage;
	std::vector<std::string> sameAs;			///< IRIs: ORCID, Wikidata, Wikipedia

	std::string photoFile;						///< the name of the file at Commons ("Yoshua Bengio 2019 cropped.jpg")
	std::string photoUrl;						///< the picture, scaled by Commons
	std::string photoPage;						///< the page of the picture at Commons
	std::string photoLicense;					///< "CC BY 4.0"
	std::string photoCredit;					///< the photographer: "Maryse Boyce"
};

/** @brief `https://pub.orcid.org/v3.0/<orcid>/<part>`, part: person or employments (the public API, read without a login). */
std::string OrcidUrl(const std::string& orcid, const std::string& part);

/** @brief The entries of Wikidata with this ORCID iD (property P496). */
std::string WikidataSearchUrl(const std::string& orcid);

/** @brief An entry of Wikidata: the claims, the English description and the link to the English Wikipedia. */
std::string WikidataEntityUrl(const std::string& wikidataId);

/** @brief The file at Commons with its address (scaled to `width`), license and photographer. */
std::string CommonsImageUrl(const std::string& photoFile, int width = 256);

/** @brief Name, biography, homepage and the ORCID iD itself from `/person`; false if the body is not that. */
bool ParseOrcidPerson(const std::string& json, PersonData* person);

/** @brief The current employment (else the first) from `/employments` as the job title; false if the body is not that. */
bool ParseOrcidEmployments(const std::string& json, PersonData* person);

/** @brief The id (`Q...`) of the first entry of a Wikidata search; false if there is none (not every researcher is notable). */
bool ParseWikidataSearch(const std::string& json, std::string* wikidataId);

/** @brief From an entry of Wikidata: the photo (P18), the birth date (P569), the homepage (P856) and the description, which only fill what ORCID did not say. */
bool ParseWikidataEntity(const std::string& json, const std::string& wikidataId, PersonData* person);

/** @brief From the answer of Commons about a file: the address of the picture, the page, the license and the photographer. */
bool ParseCommonsImage(const std::string& json, PersonData* person);

}	// namespace paperboy
