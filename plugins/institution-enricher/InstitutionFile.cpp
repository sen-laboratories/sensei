/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "InstitutionFile.h"

#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
#include <TypeConstants.h>
#include <fs_attr.h>

#include <algorithm>
#include <climits>
#include <cstring>

#include <sen/SenList.h>
#include <sen/SenOntoPeople.h>

#include "../../common/EntityFile.h"
#include "ListSeparator.h"
#include "core/InstitutionApi.h"

namespace campus {

namespace {

namespace attr = sen::onto::people::attr;

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


/** the values are added to the list that the file has (`overwrite`: they are the list); the separator is the one that the ontology says for the attribute */
void
PutList(BNode& node, const char* attribute, const std::vector<std::string>& values, bool overwrite)
{
	if (values.empty())
		return;
	const char separator = campus::ListSeparatorOf(sen::onto::people::mime::kInstitution, attribute);
	std::vector<std::string> list;
	BString existing;
	if (!overwrite && node.ReadAttrString(attribute, &existing) == B_OK && !existing.IsEmpty())
		list = campus::SplitValues(existing.String(), separator);
	for (const std::string& value : values) {
		if (std::find(list.begin(), list.end(), value) == list.end())
			list.push_back(value);
	}
	const std::string joined = sen::list::Join(list, separator);
	node.WriteAttr(attribute, B_STRING_TYPE, 0, joined.c_str(), joined.size() + 1);
}


void
PutCount(BNode& node, const char* attribute, int64_t value)
{
	if (value < 0)
		return;
	const int32 count = value > INT32_MAX ? INT32_MAX : (int32) value;
	node.WriteAttr(attribute, B_INT32_TYPE, 0, &count, sizeof(count));
}

const size_t kKeywordTopics = 8;

/** the names of the most frequent topics (OpenAlex gives them in that order) */
std::vector<std::string>
TopicNames(const institution::Record& record, size_t most)
{
	std::vector<std::string> names;
	for (const institution::Topic& topic : record.topics) {
		if (names.size() >= most)
			break;
		names.push_back(topic.name);
	}
	return names;
}

}	// namespace

status_t
ReadInstitution(const entry_ref& file, FileState* state)
{
	BNode node(&file);
	if (node.InitCheck() != B_OK)
		return node.InitCheck();

	BString text;
	if (node.ReadAttrString(attr::kName, &text) == B_OK && !text.IsEmpty()) {
		state->name = text.String();
		state->hasName = true;
	} else {
		// the name of the file, as it is: an underscore is a blank, an extension of a few letters is not part of the name
		BString name(file.name);
		name.ReplaceAll('_', ' ');
		const int32 dot = name.FindLast('.');
		if (dot > 0 && name.Length() - dot <= 5)
			name.Truncate(dot);
		state->name = name.String();
	}

	text.SetTo("");
	if (node.ReadAttrString(attr::kRor, &text) == B_OK)
		state->rorId = institution::BareRorId(text.String());

	text.SetTo("");
	if (node.ReadAttrString(attr::kSameAs, &text) == B_OK && !text.IsEmpty()) {
		const char separator = campus::ListSeparatorOf(sen::onto::people::mime::kInstitution, attr::kSameAs);
		state->sameAs = campus::SplitValues(text.String(), separator);
		// (a link to the registry is its id as well)
		if (state->rorId.empty()) {
			for (const std::string& link : state->sameAs) {
				if (link.find("ror.org/") != std::string::npos) {
					state->rorId = institution::BareRorId(link);
					if (!state->rorId.empty())
						break;
				}
			}
		}
	}
	return B_OK;
}


status_t
WriteInstitution(const entry_ref& file, const institution::Record& record, bool overwrite)
{
	BNode node(&file);
	if (node.InitCheck() != B_OK)
		return node.InitCheck();

	PutString(node, attr::kName, record.name, overwrite);
	PutString(node, attr::kRor, record.rorId, overwrite);
	PutString(node, attr::kHomepage, record.homepage, overwrite);
	PutString(node, attr::kLocality, record.locality, overwrite);
	PutString(node, attr::kCountry, record.country, overwrite);
	PutList(node, attr::kInstitutionType, record.types, overwrite);
	PutList(node, attr::kAlternateName, record.otherNames, overwrite);
	PutList(node, attr::kSameAs, record.sameAs, false);
	PutList(node, attr::kSubject, TopicNames(record, kKeywordTopics), overwrite);
	// what is counted is the state of the day, whatever the file had
	PutCount(node, attr::kWorksCount, record.worksCount);
	PutCount(node, attr::kCitationCount, record.citedByCount);

	// (a file that was no institution yet is one now)
	char type[B_MIME_TYPE_LENGTH];
	type[0] = '\0';
	BNodeInfo info(&node);
	if (info.GetType(type) != B_OK || type[0] == '\0')
		info.SetType(sen::onto::people::mime::kInstitution);
	return B_OK;
}


status_t
FindOrMakeInstitution(const entry_ref& beside, const std::string& name, const std::string& rorId, entry_ref* found, bool* created)
{
	status_t result = sensei::FindOrCreateEntityFile(beside, sen::onto::people::mime::kInstitution, name.c_str(), rorId.c_str(),
		[&rorId](BNode& node) {
			// the institution that is meant: the one with the id, or one of that name that has none (made by hand)
			BString id;
			node.ReadAttrString(attr::kRor, &id);
			const std::string bare = institution::BareRorId(id.String());
			return bare.empty() || bare == rorId;
		}, found, created);
	if (result != B_OK)
		return result;
	if (*created) {
		BNode node(found);
		PutString(node, attr::kName, name, false);
		PutString(node, attr::kRor, rorId, false);
		PutList(node, attr::kSameAs, std::vector<std::string>{"https://ror.org/" + rorId}, false);
	}
	return B_OK;
}

}	// namespace campus
