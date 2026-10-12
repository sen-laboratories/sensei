/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "GoScanner.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include <FindDirectory.h>
#include <Path.h>


namespace {

bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r'; }
bool IsIdentifierChar(char c) { return isalnum((unsigned char) c) || c == '_' || (unsigned char) c >= 0x80; }


/** reads the Go source: the imports at the top, the rest is skipped */
class Reader {
public:
	Reader(const std::string& text, std::vector<Dependency>* out)
		:
		fText(text),
		fOut(out),
		fLength(text.size())
	{
	}

	void Run()
	{
		int depth = 0;			// braces and brackets: imports are only at the top level
		bool lineStart = true;

		while (fPos < fLength) {
			char c = fText[fPos];
			if (c == '\n') {
				fLine++;
				fPos++;
				lineStart = true;
				continue;
			}
			if (IsBlank(c)) {
				fPos++;
				continue;
			}
			if (SkipComment())
				continue;
			if (c == '"' || c == '`' || c == '\'') {
				SkipLiteral();
				lineStart = false;
				continue;
			}
			if (c == '{' || c == '(' || c == '[') {
				depth++;
			} else if ((c == '}' || c == ')' || c == ']') && depth > 0) {
				depth--;
			}
			if (IsIdentifierChar(c)) {
				size_t start = fPos;
				while (fPos < fLength && IsIdentifierChar(fText[fPos]))
					fPos++;
				if (lineStart && depth == 0 && fText.compare(start, fPos - start, "import") == 0 && fPos - start == 6)
					ReadImport();
				lineStart = false;
				continue;
			}
			if (c == ';')
				lineStart = true;
			else
				lineStart = false;
			fPos++;
		}
	}

private:
	/** at a comment: skips it (and counts the lines) and returns true */
	bool SkipComment()
	{
		if (fText[fPos] != '/' || fPos + 1 >= fLength)
			return false;
		if (fText[fPos + 1] == '/') {
			while (fPos < fLength && fText[fPos] != '\n')
				fPos++;
			return true;
		}
		if (fText[fPos + 1] == '*') {
			fPos += 2;
			while (fPos < fLength && !(fText[fPos] == '*' && fPos + 1 < fLength && fText[fPos + 1] == '/')) {
				if (fText[fPos] == '\n')
					fLine++;
				fPos++;
			}
			fPos = std::min(fLength, fPos + 2);
			return true;
		}
		return false;
	}

	void SkipSpace()
	{
		while (fPos < fLength) {
			if (IsBlank(fText[fPos])) {
				fPos++;
			} else if (fText[fPos] == '\n') {
				fLine++;
				fPos++;
			} else if (!SkipComment()) {
				break;
			}
		}
	}

	/** a string, raw string or rune; returns its content without the quotes (escapes are not translated) */
	std::string SkipLiteral()
	{
		const char quote = fText[fPos++];
		size_t start = fPos;
		while (fPos < fLength) {
			char c = fText[fPos];
			if (quote != '`' && c == '\\' && fPos + 1 < fLength) {
				fPos += 2;
				continue;
			}
			if (c == '\n') {
				fLine++;
				if (quote != '`') {
					break;		// an unterminated string ends with the line
				}
			}
			if (c == quote)
				break;
			fPos++;
		}
		std::string content = fText.substr(start, fPos - start);
		if (fPos < fLength && fText[fPos] == quote)
			fPos++;
		return content;
	}

	/** an optional name (`f`, `.`, `_`) and the path in quotes */
	bool ReadSpec()
	{
		SkipSpace();
		if (fPos >= fLength)
			return false;
		if (fText[fPos] == '.' || IsIdentifierChar(fText[fPos])) {
			while (fPos < fLength && (fText[fPos] == '.' || IsIdentifierChar(fText[fPos])))
				fPos++;
			SkipSpace();
		}
		if (fPos < fLength && (fText[fPos] == '"' || fText[fPos] == '`')) {
			const unsigned line = fLine;
			std::string path = SkipLiteral();
			if (!path.empty())
				fOut->push_back({path, std::string(), true, line});
			return true;
		}
		return false;
	}

	void ReadImport()
	{
		SkipSpace();
		if (fPos < fLength && fText[fPos] == '(') {
			fPos++;
			for (;;) {
				SkipSpace();
				while (fPos < fLength && fText[fPos] == ';') {
					fPos++;
					SkipSpace();
				}
				if (fPos >= fLength)
					return;
				if (fText[fPos] == ')') {
					fPos++;
					return;
				}
				if (!ReadSpec())
					return;		// not an import group: stop
			}
		}
		ReadSpec();
	}

	const std::string&		fText;
	std::vector<Dependency>*	fOut;
	size_t					fLength;
	size_t					fPos = 0;
	unsigned				fLine = 1;
};


bool IsFile(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}


std::string Parent(const std::string& path)
{
	size_t slash = path.rfind('/');
	if (slash == std::string::npos)
		return ".";
	return slash == 0 ? "/" : path.substr(0, slash);
}


/** is the folder a Go package: does it hold a .go file? */
bool IsPackage(const std::string& folder)
{
	bool found = false;
	if (DIR* dir = opendir(folder.c_str())) {
		while (struct dirent* entry = readdir(dir)) {
			size_t length = strlen(entry->d_name);
			if (length > 3 && strcmp(entry->d_name + length - 3, ".go") == 0) {
				found = true;
				break;
			}
		}
		closedir(dir);
	}
	return found;
}


std::string Normalized(const std::string& path)
{
	BPath normalized(path.c_str(), NULL, true);
	return normalized.Path() != NULL ? normalized.Path() : path;
}

}	// namespace


const char*
GoScanner::Name() const
{
	return "Go";
}


const std::vector<FileKind>&
GoScanner::FileKinds() const
{
	static const std::vector<FileKind> kinds = {{"go", "text/x-go"}};
	return kinds;
}


void
GoScanner::Scan(const std::string& text, std::vector<Dependency>* dependencies) const
{
	Reader reader(text, dependencies);
	reader.Run();
}


const std::vector<std::string>&
GoScanner::LibraryFolders() const
{
	if (fLibraryFoldersKnown)
		return fLibraryFolders;
	fLibraryFoldersKnown = true;

	if (const char* goroot = getenv("GOROOT"))
		fLibraryFolders.push_back(std::string(goroot) + "/src");
	if (const char* gopath = getenv("GOPATH"))
		fLibraryFolders.push_back(std::string(gopath) + "/src");

	BPath home;
	if (find_directory(B_USER_DIRECTORY, &home) == B_OK)
		fLibraryFolders.push_back(std::string(home.Path()) + "/go/src");
	const directory_which places[] = {B_USER_NONPACKAGED_LIB_DIRECTORY, B_USER_LIB_DIRECTORY, B_SYSTEM_NONPACKAGED_LIB_DIRECTORY,
		B_SYSTEM_LIB_DIRECTORY};
	for (directory_which place : places) {
		BPath libraries;
		if (find_directory(place, &libraries) == B_OK)
			fLibraryFolders.push_back(std::string(libraries.Path()) + "/go/src");
	}
	return fLibraryFolders;
}


void
GoScanner::Resolve(Dependency* dependency, const std::string& sourcePath) const
{
	const std::string& name = dependency->name;
	const std::string sourceFolder = Parent(sourcePath);

	// a relative import
	if (name.compare(0, 2, "./") == 0 || name.compare(0, 3, "../") == 0) {
		std::string folder = sourceFolder + "/" + name;
		if (IsPackage(folder)) {
			dependency->resolved = Normalized(folder);
			dependency->global = false;
		}
		return;
	}

	// the module of the project: go.mod above the file
	std::string root = sourceFolder;
	std::string module;
	for (int level = 0; level < 32; level++) {
		if (IsFile(root + "/go.mod")) {
			std::ifstream file(root + "/go.mod");
			std::string line;
			while (std::getline(file, line)) {
				size_t at = line.find("module");
				if (at == 0 && line.size() > 6 && (line[6] == ' ' || line[6] == '\t')) {
					size_t start = line.find_first_not_of(" \t\"", 6);
					size_t end = line.find_last_not_of(" \t\r\"");
					if (start != std::string::npos && end >= start)
						module = line.substr(start, end - start + 1);
					break;
				}
			}
			break;
		}
		std::string parent = Parent(root);
		if (parent == root)
			break;
		root = parent;
	}
	if (!module.empty()) {
		if (name == module || name.compare(0, module.size() + 1, module + "/") == 0) {
			std::string folder = root + name.substr(module.size());
			if (IsPackage(folder)) {
				dependency->resolved = Normalized(folder);
				dependency->global = false;
				return;
			}
		}
		std::string vendored = root + "/vendor/" + name;
		if (IsPackage(vendored)) {
			dependency->resolved = Normalized(vendored);
			return;
		}
	}

	for (const std::string& folder : LibraryFolders()) {
		if (IsPackage(folder + "/" + name)) {
			dependency->resolved = Normalized(folder + "/" + name);
			return;
		}
	}
}
