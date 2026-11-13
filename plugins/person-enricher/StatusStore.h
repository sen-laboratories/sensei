/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Entry.h>
#include <SupportDefs.h>

#include <string>
#include <vector>

#include "../../common/PersonStatus.h"
#include "core/PersonData.h"
#include "core/PersonEnricher.h"

namespace robin {

/**
 * The posts of a person, as Markdown files in the cache (see common/PersonStatus.h for where and how old): Robin writes them when it enriches the person, and when the personal relation
 * navigator, which shows them as the relation *status*, asks it to fetch them again because the last time is more than an hour ago. Nothing of them is stored in the person file: it has
 * the latest status and the feed, not the posts.
 */
using namespace sensei::person;

/**
 * Makes the files of the posts: `2026-11-08 18.00 - First words of the post.md` (the time of the post, so that the names sort by it), the type text/markdown, `dc:title`, `dc:date` (the
 * time of the post, as the feed said it) and `schema:url` (the address of the post on the web). The files of earlier posts that are not among these are removed (a post that was deleted is gone),
 * and the time of the fetch is noted. Nothing is removed if there are no posts: a feed that does not answer must not empty the folder.
 */
status_t WriteStatusPosts(const entry_ref& person, const PersonProfile& profile);

/**
 * A person that has a latest status (`foaf:status`) and no posts gets one file with it, `Current Status.md`, so that the relation shows what the file knows. Replaced when posts are fetched.
 * @return B_OK, B_NOT_ALLOWED if the person has no status text
 */
status_t WriteCurrentStatus(const entry_ref& person, const PersonProfile& profile);

/**
 * Fetches the posts of a person again if they were fetched more than @p maxAgeSeconds ago (or never; or @p force), and writes them, or the current status if there are none; notes the
 * time of the fetch, also when it failed (see sensei::person::kRetrySeconds). An offline machine keeps the posts it has.
 * @param profile the person as the file has it (ReadPerson()); it gets the posts
 * @return true if posts were fetched
 */
bool RefreshStatus(const entry_ref& person, PersonProfile* profile, PersonEnricher* enricher, long maxAgeSeconds, bool force = false);

}	// namespace robin
