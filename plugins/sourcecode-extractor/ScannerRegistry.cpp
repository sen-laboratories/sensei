/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "LanguageScanner.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>

#include <Node.h>
#include <Path.h>
#include <String.h>

#include <sen/Sen.h>

#include "CFamilyScanner.h"
#include "JavaScanner.h"
#include "PythonScanner.h"

/** The languages that the extractor understands: add a scanner here (see LanguageScanner.h). */
static const std::vector<const LanguageScanner*>&
Scanners()
{
	static const CFamilyScanner cFamily;
	static const JavaScanner java;
	static const PythonScanner python;
	static const std::vector<const LanguageScanner*> scanners = {&cFamily, &java, &python};
	return scanners;
}


const LanguageScanner*
ScannerForFile(const std::string& path)
{
	// the semantic type names the language (Haiku only knows that it is source code)
	BNode node(path.c_str());
	BString semanticType;
	if (node.InitCheck() == B_OK && node.ReadAttrString(sen::attr::kType, &semanticType) == B_OK) {
		for (const LanguageScanner* scanner : Scanners()) {
			const std::vector<std::string>& types = scanner->SemanticTypes();
			if (std::find(types.begin(), types.end(), semanticType.String()) != types.end())
				return scanner;
		}
	}

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
