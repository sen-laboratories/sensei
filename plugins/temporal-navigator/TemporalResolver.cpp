/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "TemporalResolver.h"

#include <FindDirectory.h>
#include <Node.h>
#include <Path.h>
#include <Query.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_attr.h>

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <sen/SenOntoTemporal.h>
#include <sen/SenTimeAttributes.h>

namespace temporal {

namespace {

bool
ReadTime(BNode& node, const char* name, long long* seconds)
{
	attr_info info;
	if (node.GetAttrInfo(name, &info) != B_OK || info.type != B_TIME_TYPE)
		return false;
	if (info.size == (off_t)sizeof(int32)) {
		int32 value;
		if (node.ReadAttr(name, B_TIME_TYPE, 0, &value, sizeof(value)) != (ssize_t)sizeof(value))
			return false;
		*seconds = value;
		return true;
	}
	if (info.size == (off_t)sizeof(int64)) {
		int64 value;
		if (node.ReadAttr(name, B_TIME_TYPE, 0, &value, sizeof(value)) != (ssize_t)sizeof(value))
			return false;
		*seconds = value;
		return true;
	}
	return false;
}


std::string
ReadText(BNode& node, const char* name)
{
	char buffer[256];
	const ssize_t length = node.ReadAttr(name, B_STRING_TYPE, 0, buffer, sizeof(buffer) - 1);
	if (length <= 0)
		return std::string();
	buffer[length] = '\0';
	return buffer;
}


/** whether a file is in a trash folder (a file that is thrown away is no event any more) */
bool
InTrash(const entry_ref& ref)
{
	BPath path(&ref);
	if (path.InitCheck() != B_OK)
		return false;
	std::string text = path.Path();
	for (char& c : text)
		c = (char)tolower((unsigned char)c);
	return text.find("/trash/") != std::string::npos;
}

}	// namespace


bool
ReadTimeAttributes(const entry_ref& file, TimeAttributes* attributes)
{
	BNode node(&file);
	if (node.InitCheck() != B_OK)
		return false;
	namespace attr = sen::onto::temporal::attr;
	TimeAttributes found;
	found.hasStart = ReadTime(node, attr::kStartDate, &found.start);
	found.hasEnd = ReadTime(node, attr::kEndDate, &found.end);
	found.startEdtf = ReadText(node, attr::kStartDateEdtf);
	found.endEdtf = ReadText(node, attr::kEndDateEdtf);
	found.duration = ReadText(node, attr::kDuration);
	*attributes = found;
	return found.hasStart;
}


status_t
FindTimedFiles(std::vector<entry_ref>* refs, size_t limit)
{
	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK && refs->size() < limit) {
		if (!volume.KnowsQuery())
			continue;
		// every file that has a start (the attribute is indexed, as the 64-bit integer that holds a time): an event, or any other file that says when it is. A time can be before 1970.
		BQuery query;
		query.SetVolume(&volume);
		query.PushAttr(sen::onto::temporal::attr::kStartDate);
		query.PushInt64(-(1LL << 60));
		query.PushOp(B_GE);
		if (query.Fetch() != B_OK)
			continue;
		entry_ref ref;
		while (query.GetNextRef(&ref) == B_OK && refs->size() < limit) {
			if (!InTrash(ref))
				refs->push_back(ref);
		}
	}
	return B_OK;
}


Resolver::Resolver(bool debug)
	:
	fDebug(debug)
{
}


status_t
Resolver::Resolve(const entry_ref& file, const std::string& relationType, std::vector<Target>* targets)
{
	TimeAttributes own;
	if (!ReadTimeAttributes(file, &own))
		return B_ENTRY_NOT_FOUND;
	const Interval interval = IntervalOf(own, sen::timeattr::LocalOffsetAt);
	if (!interval.valid)
		return B_ENTRY_NOT_FOUND;

	BNode ownNode(&file);
	node_ref ownId;
	ownNode.GetNodeRef(&ownId);

	std::vector<entry_ref> candidates;
	FindTimedFiles(&candidates);
	for (const entry_ref& candidate : candidates) {
		BNode node(&candidate);
		node_ref id;
		if (node.GetNodeRef(&id) == B_OK && id == ownId)
			continue;
		TimeAttributes other;
		if (!ReadTimeAttributes(candidate, &other))
			continue;
		const Interval otherInterval = IntervalOf(other, sen::timeattr::LocalOffsetAt);
		const Relation relation = Compare(interval, otherInterval);
		if (relation == Relation::kNone || TypeOf(relation) != relationType)
			continue;
		if (fDebug)
			printf("%s %s %s\n", file.name, LabelOf(relation).c_str(), candidate.name);
		Target target;
		target.ref = candidate;
		target.relation = relation;
		target.start = otherInterval.start;
		targets->push_back(target);
	}
	std::stable_sort(targets->begin(), targets->end(), [](const Target& a, const Target& b) { return a.start < b.start; });
	return B_OK;
}

}	// namespace temporal
