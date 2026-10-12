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
#include "GoScanner.h"
#include "JavaScanner.h"
#include "JavaScriptScanner.h"
#include "PythonScanner.h"
#include "RustScanner.h"

/** The languages that the extractor understands: add a scanner here (see LanguageScanner.h). */
static const std::vector<const LanguageScanner*>&
Scanners()
{
	static const CFamilyScanner cFamily;
	static const GoScanner go;
	static const JavaScanner java;
	static const JavaScriptScanner javaScript;
	static const PythonScanner python;
	static const RustScanner rust;
	static const std::vector<const LanguageScanner*> scanners = {&cFamily, &go, &java, &javaScript, &python, &rust};
	return scanners;
}


bool
LanguageScanner::HandlesType(const std::string& type) const
{
	for (const FileKind& kind : FileKinds()) {
		if (kind.semanticType == type)
			return true;
	}
	return false;
}


Identification
IdentifyFile(const std::string& path, const std::string& text)
{
	Identification result;

	// by the extension
	size_t dot = path.rfind('.');
	size_t slash = path.rfind('/');
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
		std::string extension = path.substr(dot + 1);
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
		for (const LanguageScanner* scanner : Scanners()) {
			for (const FileKind& kind : scanner->FileKinds()) {
				if (kind.extension == extension) {
					result.scanner = scanner;
					result.semanticType = scanner->RefineType(kind.semanticType, text);
					return result;
				}
			}
		}
	}

	// by what the content tells (scripts without an extension)
	for (const LanguageScanner* scanner : Scanners()) {
		std::string type = scanner->TypeFromContent(text);
		if (!type.empty()) {
			result.scanner = scanner;
			result.semanticType = type;
			return result;
		}
	}

	// by a type that the file has (set by someone else)
	BNode node(path.c_str());
	BString existing;
	if (node.InitCheck() == B_OK && node.ReadAttrString(sen::attr::kType, &existing) == B_OK) {
		for (const LanguageScanner* scanner : Scanners()) {
			if (scanner->HandlesType(existing.String())) {
				result.scanner = scanner;
				result.semanticType = existing.String();
				return result;
			}
		}
	}
	return result;
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
