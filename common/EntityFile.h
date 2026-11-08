/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file EntityFile.h
 * @brief Finding and making the file of an entity (a person, a movie): in the Library of the current context, where SEN keeps them.
 *
 * A plugin that makes entities for a file (the authors of a book, the director of a movie) used to put them next to the file. Now a new entity goes into the Library of the current
 * context (the folder of its type in it: `~/Library/People`), which the SEN server tells (sen::library::FolderFor()); a server that does not answer leaves the old place, next to the
 * file. An entity that is there already is found where it is, in the Library or next to the file (where an earlier version put it), and updated in place, so that nothing is made twice.
 */

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Node.h>
#include <NodeInfo.h>
#include <String.h>

#include <string.h>

#include <functional>
#include <vector>

#include <Messenger.h>

#include <sen/SenLibrary.h>
#include <sen/SenMessages.h>

namespace sensei {

/**
 * Puts a file that was made into what the user has made sticky (the current context, a project or a label that was locked with Shift+click in the menu of SEN): the server gives it
 * a relation of the association type to each of them, as it does for a file that the file manager made. Nothing happens if there is no server or nothing is sticky.
 */
inline void
ApplyStickyClassifications(const entry_ref& file)
{
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid())
		return;
	BMessage request(sen::cmd::kClassificationApplySticky);
	request.AddRef(sen::key::kSourceRef, &file);
	BMessage reply;
	server.SendMessage(&request, &reply, 3000000, 3000000);
}

/**
 * The same without waiting for the answer: for a plugin that the server is waiting for. The server cannot read while it waits, so its queue fills up after a few dozen entities and a
 * message would block until the server gives up on the plugin: it waits one second at most and goes on (the entity is made, it is only not put into the sticky context).
 */
inline void
ApplyStickyClassificationsLater(const entry_ref& file)
{
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid())
		return;
	BMessage request(sen::cmd::kClassificationApplySticky);
	request.AddRef(sen::key::kSourceRef, &file);
	server.SendMessage(&request, (BHandler*)NULL, 1000000);
}

/**
 * The file of an entity of a type with a name.
 *
 * Looked for by name in the Library and next to @p beside: a file of the type that @p isSame accepts (the entity that is meant: the same key, the same iD; it gets the node) is
 * the one. A file of the name that is another entity, or no entity of the type, is not touched: the entity is then called `name (disambiguator)`. If there is none, one is made in the
 * Library of the current context if the server answers, else next to @p beside.
 *
 * @param beside the file that the entity is made for
 * @param mimeType the entity type (`entity/x-vnd.sen-labs.entity.person`)
 * @param name the name of the entity, which becomes the name of the file
 * @param disambiguator what is added to the name if another entity has it (a key, an iD)
 * @param isSame whether a file of that name and type is the entity (NULL: any such file is)
 * @param found gets the file
 * @param created true if the file was made
 * @param classify whether a new file is put into what is sticky (false for the many files that a plugin makes at once: the points of interest of a place)
 * @param library the folder to make the file in (NULL: the Library of the current context, asked of the server); a plugin that was asked by the server (sensei::cmd::kResolve) is given it
 */
inline status_t
FindOrCreateEntityFile(const entry_ref& beside, const char* mimeType, const char* name, const char* disambiguator, const std::function<bool(BNode&)>& isSame,
	entry_ref* found, bool* created, const entry_ref* library = NULL, bool classify = true)
{
	BString fileName(name);
	fileName.ReplaceAll('/', '-');		// (a file name cannot hold a path)
	fileName.Trim();
	if (fileName.IsEmpty())
		fileName = disambiguator;
	if (fileName.IsEmpty())
		return B_BAD_VALUE;

	// where to look: the Library first, which is also where a new file goes; else the folder of the file (the place of earlier versions)
	std::vector<BDirectory> folders;
	entry_ref libraryFolder;
	// (a plugin that the server asked and waits for (kResolve) is given the folder: it cannot ask the server, which cannot answer while it waits)
	if (library != NULL || sen::library::FolderFor(mimeType, &libraryFolder) == B_OK) {
		BDirectory directory(library != NULL ? library : &libraryFolder);
		if (directory.InitCheck() == B_OK)
			folders.push_back(directory);
	}
	BDirectory parent;
	BEntry besideEntry(&beside);
	if (besideEntry.InitCheck() == B_OK && besideEntry.GetParent(&parent) == B_OK)
		folders.push_back(parent);
	if (folders.empty())
		return B_ERROR;
	const size_t target = 0;

	BString names[2] = {fileName, BString(fileName) << " (" << disambiguator << ")"};
	for (const BString& candidate : names) {
		bool taken = false;
		for (size_t i = 0; i < folders.size(); i++) {
			BEntry entry(&folders[i], candidate.String());
			if (!entry.Exists())
				continue;
			BNode node(&entry);
			char type[B_MIME_TYPE_LENGTH];
			type[0] = '\0';
			BNodeInfo(&node).GetType(type);
			if (strcmp(type, mimeType) == 0 && (!isSame || isSame(node))) {
				*created = false;
				return entry.GetRef(found);
			}
			if (i == target)
				taken = true;
		}
		if (taken)
			continue;

		BFile file;
		status_t result = folders[target].CreateFile(candidate.String(), &file, true);
		if (result != B_OK)
			return result;
		file.Sync();		// (the attributes are written to the file as it is now)
		result = BNodeInfo(&file).SetType(mimeType);
		if (result != B_OK)
			return result;
		*created = true;
		status_t made = BEntry(&folders[target], candidate.String()).GetRef(found);
		if (made == B_OK) {
			// (a new entity is in the current context, if that is sticky; a plugin that the server waits for does not wait for the server: it is told, and answers later)
			if (!classify)
				;
			else if (library != NULL)
				ApplyStickyClassificationsLater(*found);
			else
				ApplyStickyClassifications(*found);
		}
		return made;
	}
	return B_NAME_IN_USE;
}

}	// namespace sensei
