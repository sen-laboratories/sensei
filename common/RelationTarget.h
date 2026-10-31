/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file RelationTarget.h
 * @brief The target of the file of a relation (header only).
 *
 * The file of a relation in a relation view is a proxy for the target of the relation: it carries the target as SEN:REL:TRG. A navigator that is
 * asked to open such a file opens the target. Any other file (an item of a menu is the target itself) is returned as it is.
 */

#include <Entry.h>
#include <Message.h>
#include <Node.h>
#include <fs_attr.h>

#include <sen/Sen.h>

namespace sensei {

/**
 * @param ref the file that a navigator was asked to open
 * @param found false if it is the file of a relation whose target does not exist any more (the result is then ref)
 * @return the target of the relation, or ref itself if it is not the file of a relation
 */
inline entry_ref
RelationTarget(const entry_ref& ref, bool* found)
{
	*found = true;

	BEntry entry(&ref);
	BNode node(&ref);
	attr_info info;
	if (entry.InitCheck() != B_OK || entry.IsDirectory() || node.InitCheck() != B_OK
			|| node.GetAttrInfo(sen::attr::kRelationTargetRef, &info) != B_OK || info.type != B_REF_TYPE || info.size <= 0)
		return ref;

	entry_ref target = ref;
	*found = false;
	char* buffer = new char[info.size];
	if (node.ReadAttr(sen::attr::kRelationTargetRef, B_REF_TYPE, 0, buffer, info.size) == info.size) {
		// the ref is stored as a message holds it
		BMessage holder;
		entry_ref stored;
		if (holder.AddData("ref", B_REF_TYPE, buffer, info.size, false) == B_OK && holder.FindRef("ref", &stored) == B_OK
				&& BEntry(&stored).Exists()) {
			target = stored;
			*found = true;
		}
	}
	delete[] buffer;
	return target;
}

}	// namespace sensei
