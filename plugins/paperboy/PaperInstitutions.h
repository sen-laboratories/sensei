/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <SupportDefs.h>

#include <string>
#include <vector>

#include "PaperAffiliations.h"

/**
 * @brief The institutions of an author, as institution files, and the Affiliation relation of the person to each.
 *
 * What the paper and ORCID say (see PlanAffiliations()) is written as far as it is known: the name, the ROR id, the country, the city and the type. Nothing is fetched here: the
 * SEN Campus plugin (the institution enricher) adds what the registries know when the institution is enriched. The institutions that the paper names are also the ones that the paper
 * comes from: the paper gets the relation to each of them. An institution is found by its ROR id wherever it is on the volume, so
 * that the one that was enriched before (and may be named otherwise) is not made twice; what the file has stays, only what is missing is filled in.
 */
class InstitutionMaker {
public:
	InstitutionMaker();

	/**
	 * Makes the institutions of the stays and relates the person to them. A stay that cannot be made or related is skipped, with a message.
	 * @param person the author (a file of the type person)
	 * @param paper the paper (the institutions are made in the Library, else next to it)
	 * @return the number of affiliations that are there now
	 */
	int32			Affiliate(const entry_ref& person, const entry_ref& paper, const std::vector<paperboy::Stay>& stays);

	/** how many institution files were made (not found) */
	int32			Made() const { return fMade; }

private:
	status_t		_FindOrCreate(const entry_ref& paper, const paperboy::Stay& stay, entry_ref* found, bool* created);
	bool			_FindByRor(const std::string& rorId, const entry_ref& near, entry_ref* found);
	void			_WriteAttributes(const entry_ref& institution, const paperboy::Stay& stay);
	status_t		_Relate(const entry_ref& person, const entry_ref& institution, const paperboy::Stay& stay);
	/** the paper comes from the institution (the Source Institution relation, stored at the paper: the same institution of several authors is one relation) */
	status_t		_RelatePaper(const entry_ref& paper, const entry_ref& institution);

	int32			fMade;
};
