/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>

#include <string>
#include <vector>

#include "TemporalRelations.h"

namespace temporal {

/** a file that is related to the file that was asked about, and how */
struct Target {
	entry_ref	ref;
	Relation	relation = Relation::kNone;	///< of the file that was asked about to this one (before, meets...)
	long long	start = 0;					///< where its interval begins, to put them in the order of time
};

/** The time that a file says of itself (`schema:startDate`, `schema:endDate`, `schema:duration`, the text of a time that is not exact). False if it says none. */
bool ReadTimeAttributes(const entry_ref& file, TimeAttributes* attributes);

/** The events (a query of the type on every volume), the ones in the trash left out. At most @p limit. */
status_t FindTimedFiles(std::vector<entry_ref>* refs, size_t limit = 20000);

class Resolver {
public:
							Resolver(bool debug);

	/**
	 * The files that the file is related to in the way of the relation type (one of the five of the ontology: before and after, meets and met by...): each is a target with the relation
	 * as it is seen from the file, in the order of time. B_ENTRY_NOT_FOUND if the file says no time of its own.
	 */
	status_t				Resolve(const entry_ref& file, const std::string& relationType, std::vector<Target>* targets);

private:
	bool					fDebug;
};

}	// namespace temporal
