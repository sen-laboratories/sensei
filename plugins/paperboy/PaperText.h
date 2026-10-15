/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

#include <SupportDefs.h>

/** What can be read from the beginning of a document without asking anybody: the fields of the document and the text of the first pages. */
struct DocumentFacts {
	/** the text of each page that was read (the first pages) */
	std::vector<std::string> pages;
	/** values of the fields of the document that may hold a DOI (Subject, Keywords, doi, ...) */
	std::vector<std::string> metadata;
	/** the Title field of the document; word processors fill it with junk, see paperboy::LooksLikeTitle() */
	std::string infoTitle;
	/** the biggest text of the first page, which is the title of a paper more often than not */
	std::string headline;
	int pageCount = 0;
};

/**
 * @brief Reads the fields and the text of the first pages of a PDF or EPUB with MuPDF.
 * @param maxPages how many pages from the start are read
 * @return B_OK, B_NOT_ALLOWED for a document that needs a password, B_BAD_DATA for a document that cannot be read
 */
status_t ReadDocumentFacts(const std::string& path, int maxPages, DocumentFacts* facts);
