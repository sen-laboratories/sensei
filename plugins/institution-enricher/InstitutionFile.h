/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Entry.h>
#include <String.h>

#include <string>
#include <vector>

#include "core/InstitutionModel.h"

namespace campus {

/** what the file of an institution says now */
struct FileState {
	std::string					name;		///< foaf:name, else the name of the file
	std::string					rorId;		///< the bare id, from dc:identifier or a link of schema:sameAs
	std::vector<std::string>	sameAs;
	bool						hasName = false;
};

/** reads what the file says; B_OK if it could be read */
status_t ReadInstitution(const entry_ref& file, FileState* state);

/**
 * Writes what the registries found into the attributes of the file. What the file has is kept (the lists are completed), @p overwrite replaces it; the counts are always the latest.
 */
status_t WriteInstitution(const entry_ref& file, const institution::Record& record, bool overwrite);

/** Makes the entity file of an institution that a relation ends at (the parent, a unit): the name, the ROR id and the link, nothing else; found again by the id. Returns the file. */
status_t FindOrMakeInstitution(const entry_ref& beside, const std::string& name, const std::string& rorId, entry_ref* found, bool* created);

}	// namespace campus
