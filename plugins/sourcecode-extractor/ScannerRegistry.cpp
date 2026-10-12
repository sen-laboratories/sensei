/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "LanguageScanner.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>

#include <Path.h>

#include "CFamilyScanner.h"

/** The languages that the extractor understands: add a scanner here (see LanguageScanner.h). */
static const std::vector<const LanguageScanner*>&
Scanners()
{
	static const CFamilyScanner cFamily;
	static const std::vector<const LanguageScanner*> scanners = {&cFamily};
	return scanners;
}


const LanguageScanner*
ScannerForFile(const std::string& path)
{
	size_t dot = path.rfind('.');
	size_t slash = path.rfind('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return NULL;

	std::string extension = path.substr(dot + 1);
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });

	for (const LanguageScanner* scanner : Scanners()) {
		const std::vector<std::string>& extensions = scanner->Extensions();
		if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end())
			return scanner;
	}
	return NULL;
}


std::string
FindFileInFolders(const std::vector<std::string>& folders, const std::string& name)
{
	for (const std::string& folder : folders) {
		std::string candidate = folder + "/" + name;
		struct stat st;
		if (stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
			BPath path(candidate.c_str(), NULL, true);	// normalized
			return path.Path() != NULL ? path.Path() : candidate;
		}
	}
	return std::string();
}
