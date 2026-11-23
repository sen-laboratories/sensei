/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Entry.h>

#include "core/InstitutionModel.h"

namespace campus {

struct RelationResult {
	int made = 0;		///< the files that were made (the larger institution, the units)
	int related = 0;	///< the relations that are there now
};

/**
 * The relations of an enriched institution, from what the registries say: Part Of, to the institution that the registry names as its parent, and from each of its children to it
 * (the first @p maxUnits: some institutions have hundreds, so by default there are none). The institutions that are not on the computer yet are made (with their name and id) in the Library, and found again by their id.
 * (The topics are keywords of the file, no relations: a topic has no place on the computer to be a file in yet.)
 * Nothing is made if the SEN server is not running for the relations (the files are made all the same); the next enrichment makes them.
 */
RelationResult MakeRelations(const entry_ref& institution, const institution::Record& record, int maxUnits, bool debug);

}	// namespace campus
