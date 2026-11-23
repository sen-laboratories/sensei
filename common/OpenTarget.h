/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file OpenTarget.h
 * @brief Opens the target of a relation the way a double click in the file manager does (header only).
 *
 * A file has a technical type (BEOS:TYPE: text/calendar) and may have a semantic type that says what it is (META:TYPE: entity/x-vnd.sen-labs.entity.event). A file of an entity type opens with the
 * application of that type (Shoji), also when its technical type is another one: that is what the file manager (Akita) does with it, and a navigator has to do the same, else a calendar
 * file is opened by the text editor. Any other file is opened by the application of its technical type.
 */

#include <AppFileInfo.h>
#include <Entry.h>
#include <File.h>
#include <Message.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Roster.h>
#include <String.h>

#include <strings.h>

#include <sen/Sen.h>
#include <sen/SenAttributes.h>

namespace sensei {

/**
 * The type of the target that a navigator was told: the one Akita puts into the message for an item of a menu (the target itself), else the semantic type (META:TYPE) of the file that it was
 * asked to open, which for the file of a relation in a relation view is the type of its target (what `SEN:materialiseAs` of the relation says, or the semantic type of the target) while the
 * technical type (BEOS:TYPE) of that file is the relation. Empty if it was not told.
 */
inline BString
TargetTypeHint(const BMessage* message, const entry_ref& file)
{
	BString type(message != NULL ? message->GetString(sen::key::kTargetType, "") : "");
	if (type.IsEmpty()) {
		BNode node(&file);
		if (node.InitCheck() == B_OK)
			node.ReadAttrString(sen::attr::kType, &type);
	}
	return type;
}

/**
 * @param typeHint the type that the target is opened as, if it is told (see TargetTypeHint()); else its own semantic type, if it has one
 * @return what be_roster->Launch returns (B_ALREADY_RUNNING is as good as B_OK for a program that was started before)
 */
inline status_t
OpenTarget(const entry_ref& target, const char* typeHint = NULL)
{
	BNode node(&target);
	BString semanticType(typeHint != NULL ? typeHint : "");
	if (semanticType.IsEmpty() && node.InitCheck() == B_OK)
		node.ReadAttrString(sen::attr::kType, &semanticType);
	if (!semanticType.IsEmpty() && node.InitCheck() == B_OK) {
		char fileType[B_MIME_TYPE_LENGTH];
		const bool same = BNodeInfo(&node).GetType(fileType) == B_OK && strcasecmp(fileType, semanticType.String()) == 0;
		entry_ref application;
		if (!same && BMimeType(semanticType.String()).IsInstalled() && be_roster->FindApp(semanticType.String(), &application) == B_OK) {
			BMessage refs(B_REFS_RECEIVED);
			refs.AddRef("refs", &target);
			status_t result = be_roster->Launch(&application, &refs);
			return result == B_ALREADY_RUNNING ? B_OK : result;
		}
	}
	status_t result = be_roster->Launch(&target);
	return result == B_ALREADY_RUNNING ? B_OK : result;
}

}	// namespace sensei
