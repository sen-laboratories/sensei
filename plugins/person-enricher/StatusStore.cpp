/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "StatusStore.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <String.h>
#include <fs_attr.h>

#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <set>

#include <sen/SenAttributes.h>

#include "core/FeedResolver.h"
#include "core/FeedText.h"

namespace robin {

static const char* const kCurrentStatusName = "Current Status.md";

static bool
IsMarkdown(const char* name)
{
	const size_t length = strlen(name);
	return length > 3 && strcmp(name + length - 3, ".md") == 0;
}


static status_t
WriteMarkdown(const std::string& folder, const std::string& fileName, const std::string& content, const std::string& title, int64_t epoch, const std::string& url)
{
	BPath path(folder.c_str());
	path.Append(fileName.c_str());
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t result = file.InitCheck();
	if (result != B_OK)
		return result;
	file.Write(content.c_str(), content.size());
	file.Sync();

	BNodeInfo info(&file);
	info.SetType("text/markdown");
	file.WriteAttr("dc:title", B_STRING_TYPE, 0, title.c_str(), title.size() + 1);
	if (epoch > 0) {
		const time_t when = (time_t)epoch;
		file.WriteAttr("dc:date", B_TIME_TYPE, 0, &when, sizeof(when));
	}
	if (!url.empty())
		file.WriteAttr("schema:url", B_STRING_TYPE, 0, url.c_str(), url.size() + 1);
	return B_OK;
}


status_t
WriteStatusPosts(const entry_ref& person, const PersonProfile& profile)
{
	if (profile.recentPosts.empty())
		return B_OK;
	const std::string folder = StatusFolder(person);
	if (folder.empty() || create_directory(folder.c_str(), 0755) != B_OK)
		return B_ERROR;

	std::set<std::string> written;
	for (const FeedPost& post : profile.recentPosts) {
		// the name: the time of the post (a post that has none comes after those that have one: `0000`), then its first words
		std::string name = post.epoch != 0 ? feedtext::FileDatePrefix(post.epoch) : std::string("0000-00-00 00.00");
		const std::string words = feedtext::FileNamePart(post.title, 60);
		name += " - " + (words.empty() ? std::string("post") : words);
		std::string fileName = name + ".md";
		for (int n = 2; written.count(fileName) != 0; n++)		// (two posts of one minute with the same first words)
			fileName = name + " (" + std::to_string(n) + ").md";
		written.insert(fileName);
		WriteMarkdown(folder, fileName, FeedResolver::FormatPostToMarkdown(post, profile.name), post.title, post.epoch, feedtext::WebUrl(post.url));
	}

	// what an earlier fetch left (a post that is gone from the feed, the file of the current status) goes
	BDirectory dir(folder.c_str());
	BEntry entry;
	std::vector<BEntry> remove;
	while (dir.GetNextEntry(&entry, false) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) == B_OK && IsMarkdown(name) && written.count(name) == 0)
			remove.push_back(entry);
	}
	for (BEntry& old : remove)
		old.Remove();

	StampStatus(folder, true);
	return B_OK;
}


status_t
WriteCurrentStatus(const entry_ref& person, const PersonProfile& profile)
{
	if (profile.latestStatus.empty())
		return B_NOT_ALLOWED;
	const std::string folder = StatusFolder(person);
	if (folder.empty() || create_directory(folder.c_str(), 0755) != B_OK)
		return B_ERROR;

	std::string content = "# " + profile.name + " \xE2\x80\x94 Status\n\n" + profile.latestStatus + "\n\n";
	if (!profile.feedUrl.empty())
		content += "---\n[Source Feed](" + profile.feedUrl + ")\n";
	const std::string title = "Status: " + feedtext::TruncateUtf8(profile.latestStatus, 50);
	return WriteMarkdown(folder, kCurrentStatusName, content, title, profile.statusEpoch, std::string());
}


bool
RefreshStatus(const entry_ref& person, PersonProfile* profile, PersonEnricher* enricher, long maxAgeSeconds, bool force)
{
	const std::string folder = StatusFolder(person);
	const long age = StatusAge(folder);
	if (!force && age >= 0 && age <= maxAgeSeconds)
		return false;
	const bool fetched = enricher != NULL && enricher->RefreshPosts(profile) && WriteStatusPosts(person, *profile) == B_OK;
	// (no posts, but a status that the file knows: it is what the relation shows)
	if (!fetched && StatusFiles(folder).empty())
		WriteCurrentStatus(person, *profile);
	StampStatus(folder, fetched);
	return fetched;
}

}	// namespace robin
