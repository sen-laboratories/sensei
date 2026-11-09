/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file PersonStatus.h
 * @brief Where the posts of a person are kept, and how old they are (header only, Haiku).
 *
 * The posts of a person are Markdown files in the cache (`~/config/cache/sen/people-enricher/statuses/<key>/`). Robin (the person enricher) writes them when it enriches the person and when the
 * personal relation navigator asks it to fetch them again; the navigator lists them. This is what both need to find them.
 *
 * The folder is named by the SEN:ID of the person (else its device and inode), not by its name: two people can have the same name, and a person can be renamed.
 */

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Node.h>
#include <Path.h>
#include <String.h>
#include <fs_attr.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <string>
#include <vector>

#include <sen/SenAttributes.h>

namespace sensei {
namespace person {

/** The posts are fetched again when they are older than this (what somebody posts changes by the hour; a feed that is asked more often is a burden to its server). */
const long kRefreshSeconds = 3600;
/** After a fetch that failed (no network, a feed that is down) it is tried again after this long, not at once at every opening. */
const long kRetrySeconds = 300;

/** The name of the folder of a person's posts: its SEN:ID, else `<device>-<inode>`. */
inline std::string
PersonKey(const entry_ref& person)
{
	BNode node(&person);
	BString id;
	if (node.InitCheck() == B_OK && node.ReadAttrString(sen::attr::kId, &id) == B_OK && !id.IsEmpty()) {
		id.ReplaceAll('/', '-');
		return id.String();
	}
	node_ref ref;
	char key[64];
	if (node.InitCheck() == B_OK && node.GetNodeRef(&ref) == B_OK)
		snprintf(key, sizeof(key), "%d-%lld", (int)ref.device, (long long)ref.node);
	else
		snprintf(key, sizeof(key), "unknown");
	return key;
}


/** The path of the folder (it may not be there); empty if the cache folder cannot be found. */
inline std::string
StatusFolder(const entry_ref& person)
{
	BPath path;
	if (find_directory(B_USER_CACHE_DIRECTORY, &path) != B_OK || path.Append("sen/people-enricher/statuses") != B_OK || path.Append(PersonKey(person).c_str()) != B_OK)
		return std::string();
	return path.Path();
}


/** How long ago the posts were fetched (or tried to be: see kRetrySeconds), in seconds; -1 if they never were. */
inline long
StatusAge(const std::string& folder)
{
	if (folder.empty())
		return -1;
	BPath stamp(folder.c_str());
	stamp.Append(".fetched");
	BEntry entry(stamp.Path());
	time_t modified = 0;
	if (entry.InitCheck() != B_OK || entry.GetModificationTime(&modified) != B_OK)
		return -1;
	const time_t now = time(NULL);
	return now > modified ? (long)(now - modified) : 0;
}


/** Notes that the posts were fetched now; after a failure the note is made so that it is tried again after kRetrySeconds (the posts that are there stay). */
inline void
StampStatus(const std::string& folder, bool succeeded)
{
	if (folder.empty() || create_directory(folder.c_str(), 0755) != B_OK)
		return;
	BPath stamp(folder.c_str());
	stamp.Append(".fetched");
	BFile file(stamp.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	time_t now = time(NULL);
	file.Write(&now, sizeof(now));
	if (!succeeded) {
		const time_t earlier = now - (kRefreshSeconds - kRetrySeconds);
		file.SetModificationTime(earlier);
	}
}


/** The files of the posts, newest first (their names begin with the time of the post). */
inline std::vector<entry_ref>
StatusFiles(const std::string& folder)
{
	std::vector<entry_ref> files;
	BDirectory dir(folder.c_str());
	if (dir.InitCheck() != B_OK)
		return files;
	BEntry entry;
	while (dir.GetNextEntry(&entry, true) == B_OK) {
		entry_ref ref;
		const size_t length = entry.GetRef(&ref) == B_OK ? strlen(ref.name) : 0;
		if (entry.IsFile() && length > 3 && strcmp(ref.name + length - 3, ".md") == 0)
			files.push_back(ref);
	}
	std::sort(files.begin(), files.end(), [](const entry_ref& a, const entry_ref& b) { return strcmp(a.name, b.name) > 0; });
	return files;
}

}	// namespace person
}	// namespace sensei
