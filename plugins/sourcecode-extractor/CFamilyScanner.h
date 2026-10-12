/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "LanguageScanner.h"

/**
 * @brief C, C++ and Objective-C: `#include`, `#include_next` and `#import`.
 *
 * The text is read like a preprocessor does it as far as the directives need: comments, strings (also raw strings) and line continuations are
 * skipped, so that a directive in a comment or a string is not found; blocks that are switched off by `#if 0` (or `#if false`) and the `#else`
 * of `#if 1` are skipped. Any other condition is not evaluated: the includes of both branches are reported. Includes that are not a file name
 * (`#include MACRO`) are not reported.
 *
 * Headers are looked up like the compiler of Haiku does: `"name"` relative to the folder of the file first, then the header folders of the
 * system: the header folders of find_directory (B_USER_NONPACKAGED_HEADERS_DIRECTORY, B_USER_HEADERS_DIRECTORY, B_SYSTEM_NONPACKAGED_HEADERS_DIRECTORY
 * and B_SYSTEM_HEADERS_DIRECTORY, then what BPathFinder adds), and in each of them the folders below `os`, `posix`, `bsd`, `gnu` and `c++`.
 */
class CFamilyScanner : public LanguageScanner {
public:
	const char*	Name() const;
	const std::vector<FileKind>&	FileKinds() const;
	std::string	RefineType(const std::string& type, const std::string& text) const;
	void		Scan(const std::string& text, std::vector<Dependency>* dependencies) const;
	void		Resolve(Dependency* dependency, const std::string& sourcePath) const;

private:
	const std::vector<std::string>&	HeaderFolders() const;

	mutable	std::vector<std::string>	fHeaderFolders;
	mutable	bool						fHeaderFoldersKnown = false;
};
