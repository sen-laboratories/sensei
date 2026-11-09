/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Entry.h>
#include <SupportDefs.h>

#include "core/PersonData.h"

namespace robin {

/**
 * Reads existing profile fields from a person file (entity/x-vnd.sen-labs.entity.person or application/x-person).
 */
status_t ReadPerson(const entry_ref& file, PersonProfile* profile);

/**
 * Writes the latest status of the person and when it was posted (`foaf:status`, `SEN:statusDate`) and the feed (`foaf:weblog`) if the person has none: what a fetch of the posts changes.
 */
status_t WriteStatusAttributes(const entry_ref& file, const PersonProfile& profile);

/**
 * Writes enriched attributes, avatar thumbnail, and cached status posts to a person file.
 */
status_t WritePerson(const entry_ref& file, const PersonProfile& profile, bool overwrite = false);

} // namespace robin
