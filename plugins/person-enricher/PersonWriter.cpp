/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PersonWriter.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <String.h>
#include <TypeConstants.h>
#include <fs_attr.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include <sen/Sen.h>
#include <sen/SenList.h>
#include <sen/SenOntoPeople.h>

#include "../../common/ThumbnailUtil.h"
#include "ListSeparator.h"
#include "StatusStore.h"

namespace robin {

namespace {

bool
HasAttr(BNode& node, const char* attribute)
{
	attr_info info;
	return node.GetAttrInfo(attribute, &info) == B_OK && info.size > 0;
}

void
PutString(BNode& node, const char* attribute, const std::string& value, bool overwrite)
{
	if (value.empty() || (!overwrite && HasAttr(node, attribute)))
		return;
	node.WriteAttr(attribute, B_STRING_TYPE, 0, value.c_str(), value.size() + 1);
}

void
PutList(BNode& node, const char* attribute, const std::vector<std::string>& values, bool overwrite)
{
	if (values.empty())
		return;
	const char separator = ListSeparatorOf(sen::onto::people::mime::kPerson, attribute);
	std::vector<std::string> current;
	BString existing;
	if (node.ReadAttrString(attribute, &existing) == B_OK && !existing.IsEmpty())
		current = SplitValues(existing.String(), separator);

	for (const auto& val : values) {
		if (std::find(current.begin(), current.end(), val) == current.end())
			current.push_back(val);
	}
	std::string joined = sen::list::Join(current, separator);
	node.WriteAttr(attribute, B_STRING_TYPE, 0, joined.c_str(), joined.size() + 1);
}

} // namespace


status_t
ReadPerson(const entry_ref& file, PersonProfile* profile)
{
	if (profile == nullptr)
		return B_BAD_VALUE;

	BNode node(&file);
	status_t result = node.InitCheck();
	if (result != B_OK)
		return result;

	BString str;
	// Name: foaf:name or META:name
	if (node.ReadAttrString(sen::onto::people::attr::kName, &str) == B_OK && !str.IsEmpty())
		profile->name = str.String();
	else if (node.ReadAttrString(sen::onto::people::attr::kBeName, &str) == B_OK && !str.IsEmpty())
		profile->name = str.String();
	else
		profile->name = file.name;

	// Given / Family / Nick
	if (node.ReadAttrString(sen::onto::people::attr::kGivenName, &str) == B_OK)
		profile->givenName = str.String();
	if (node.ReadAttrString(sen::onto::people::attr::kFamilyName, &str) == B_OK)
		profile->familyName = str.String();
	if (node.ReadAttrString(sen::onto::people::attr::kNickname, &str) == B_OK && !str.IsEmpty())
		profile->nick = str.String();
	else if (node.ReadAttrString(sen::onto::people::attr::kBeNickname, &str) == B_OK && !str.IsEmpty())
		profile->nick = str.String();

	// Email: foaf:mbox or META:email
	if (node.ReadAttrString(sen::onto::people::attr::kMbox, &str) == B_OK && !str.IsEmpty())
		profile->mbox = str.String();
	else if (node.ReadAttrString(sen::onto::people::attr::kBeEmail, &str) == B_OK && !str.IsEmpty())
		profile->mbox = str.String();

	// Homepage / URL
	if (node.ReadAttrString(sen::onto::people::attr::kHomepage, &str) == B_OK && !str.IsEmpty())
		profile->homepage = str.String();
	else if (node.ReadAttrString(sen::onto::people::attr::kBeUrl, &str) == B_OK && !str.IsEmpty())
		profile->homepage = str.String();

	// Job Title / Company
	if (node.ReadAttrString(sen::onto::people::attr::kJobTitle, &str) == B_OK && !str.IsEmpty())
		profile->jobTitle = str.String();
	if (node.ReadAttrString(sen::onto::people::attr::kBeCompany, &str) == B_OK && !str.IsEmpty())
		profile->company = str.String();

	// Bio
	if (node.ReadAttrString(sen::onto::people::attr::kDescription, &str) == B_OK)
		profile->bio = str.String();

	// Same As list
	if (node.ReadAttrString(sen::onto::people::attr::kSameAs, &str) == B_OK && !str.IsEmpty()) {
		char sep = ListSeparatorOf(sen::onto::people::mime::kPerson, sen::onto::people::attr::kSameAs);
		profile->sameAs = SplitValues(str.String(), sep);
	}

	// Feed (foaf:weblog; an early version of Robin wrote it as schema:feed)
	if (node.ReadAttrString(sen::onto::people::attr::kFeed, &str) == B_OK && !str.IsEmpty())
		profile->feedUrl = str.String();
	else if (node.ReadAttrString("schema:feed", &str) == B_OK && !str.IsEmpty())
		profile->feedUrl = str.String();

	// Status
	if (node.ReadAttrString(sen::onto::people::attr::kStatus, &str) == B_OK)
		profile->latestStatus = str.String();

	return B_OK;
}


status_t
WriteStatusAttributes(const entry_ref& file, const PersonProfile& profile)
{
	BNode node(&file);
	status_t result = node.InitCheck();
	if (result != B_OK)
		return result;
	namespace attr = sen::onto::people::attr;

	// the latest status, and when it was posted (not when it was found: a status that was written a year ago says so)
	if (!profile.latestStatus.empty()) {
		PutString(node, attr::kStatus, profile.latestStatus, true);
		if (profile.statusEpoch > 0) {
			const time_t posted = (time_t)profile.statusEpoch;
			node.WriteAttr(attr::kStatusDate, B_TIME_TYPE, 0, &posted, sizeof(posted));
		} else {
			node.RemoveAttr(attr::kStatusDate);
		}
	}
	PutString(node, attr::kFeed, profile.feedUrl, false);
	return B_OK;
}


status_t
WritePerson(const entry_ref& file, const PersonProfile& profile, bool overwrite)
{
	BNode node(&file);
	status_t result = node.InitCheck();
	if (result != B_OK)
		return result;

	namespace attr = sen::onto::people::attr;

	PutString(node, attr::kName, profile.name, overwrite);
	PutString(node, attr::kGivenName, profile.givenName, overwrite);
	PutString(node, attr::kFamilyName, profile.familyName, overwrite);
	PutString(node, attr::kNickname, profile.nick, overwrite);
	PutString(node, attr::kMbox, profile.mbox, overwrite);
	PutString(node, attr::kHomepage, profile.homepage, overwrite);
	PutString(node, attr::kJobTitle, profile.jobTitle.empty() ? profile.company : profile.jobTitle, overwrite);
	PutString(node, attr::kDescription, profile.bio, overwrite);

	if (!profile.sameAs.empty())
		PutList(node, attr::kSameAs, profile.sameAs, overwrite);

	if (!profile.feedUrl.empty())
		PutString(node, attr::kFeed, profile.feedUrl, overwrite);

	WriteStatusAttributes(file, profile);

	// Write Thumbnail if image data was fetched
	if (profile.HasAvatar()) {
		attr_info info;
		if (overwrite || node.GetAttrInfo(THUMBNAIL_ATTR_NAME, &info) != B_OK || info.size <= 0) {
			if (WriteThumbnail(&file, profile.avatarData) == B_OK) {
				PutString(node, attr::kImage, profile.avatarUrl, true);
				PutString(node, attr::kPhotoCredit, profile.avatarCredit, true);
				if (!profile.avatarLicense.empty())
					PutString(node, attr::kPhotoLicense, profile.avatarLicense, true);
			}
		}
	}

	// Semantic type: ensure it is entity/x-vnd.sen-labs.entity.person
	if (!HasAttr(node, sen::attr::kType)) {
		std::string type = sen::onto::people::mime::kPerson;
		node.WriteAttr(sen::attr::kType, B_STRING_TYPE, 0, type.c_str(), type.size() + 1);
	}

	// The posts themselves are Markdown files in the cache (see StatusStore.h), which the relation *status* shows
	if (!profile.recentPosts.empty())
		WriteStatusPosts(file, profile);

	return B_OK;
}


} // namespace robin
