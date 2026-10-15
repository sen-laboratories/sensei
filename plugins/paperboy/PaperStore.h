/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

#include <Entry.h>
#include <Message.h>
#include <Node.h>

#include "PaperModel.h"

/**
 * The attributes of the file: reading what is there and writing what was found. Everything about BFS is here, so that the
 * service (Paperboy) says what it found and not how it is stored.
 */
namespace paperboy {
namespace store {

/** @brief The DOI that an earlier run stored as dc:identifier, "" if there is none or it is no DOI. */
std::string StoredDoi(BNode& node);

/** @brief The references that an earlier run stored in the state; @return false if the state has none. */
bool StoredReferences(BNode& node, std::vector<Reference>* references);

/** @brief Sets META:TYPE to the semantic type, after making sure that the file has its Haiku type (BEOS:TYPE): without it Haiku takes the file for an entry of the MIME database. */
status_t SetSemanticType(const entry_ref& file);

/**
 * @brief Writes what is known about the paper into the attributes.
 *
 * The data of the access (open access status, links, number of citations) is replaced on every run. The bibliographic data (title,
 * authors, ...) fills what is missing, unless @p overwrite is set: what a person or another program has put there stays.
 * @param overwrite replace bibliographic attributes that already have a value
 * @param references the cited works to keep in the state, so that the extraction needs no network
 */
status_t WritePaper(BNode& node, const Paper& paper, const char* doiSource, bool unpaywall, bool overwrite,
	const std::vector<Reference>& references);

/** @brief Writes the state of a run that found nothing (so people can see that the file was tried). */
status_t WriteNotFound(BNode& node, const char* status);

/**
 * @brief Writes the title that the document itself gives, for a paper that no service knows. The title fills what is missing (unless
 * @p overwrite), and the state says that it comes from the document and not from OpenAlex.
 */
status_t WriteDocumentTitle(BNode& node, const std::string& title, bool overwrite);

/**
 * @brief The file on this computer that has a DOI as its dc:identifier, searched on the volume of @p near first, then on all.
 * Needs the index on dc:identifier, which this makes for the volume of the file that is enriched (see EnsureIndex()).
 */
bool FindLocalFile(const std::string& doi, const entry_ref& near, entry_ref* found);

/** @brief Makes the BFS index of dc:identifier on the volume of a file, which queries for local papers need. Harmless if it exists. */
void EnsureIndex(const entry_ref& file);

}	// namespace store
}	// namespace paperboy
