/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PythonScanner.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>

#include <FindDirectory.h>
#include <Path.h>


namespace {

bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }
bool IsIdentifierChar(char c) { return isalnum((unsigned char) c) || c == '_' || (unsigned char) c >= 0x80; }


/** reads the Python source: finds the import statements, the rest is skipped */
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
		bool statementStart = true;
		int depth = 0;				// brackets: a line break inside is no end of the statement

		while (fPos < fLength) {
			char c = fText[fPos];

			if (SkipContinuation())
				continue;
			if (c == '\n') {
				fLine++;
				fPos++;
				if (depth == 0)
					statementStart = true;
				continue;
			}
			if (IsBlank(c)) {
				fPos++;
				continue;
			}
			if (c == '#') {
				while (fPos < fLength && fText[fPos] != '\n')
					fPos++;
				continue;
			}
			if (c == '"' || c == '\'') {
				SkipString();
				statementStart = false;
				continue;
			}
			if (c == '(' || c == '[' || c == '{') {
				depth++;
			} else if ((c == ')' || c == ']' || c == '}') && depth > 0) {
				depth--;
			} else if ((c == ';' || c == ':') && depth == 0) {
				// `a = 1; import b`, `if x: import y` (an annotation `x: int` does no harm: `int` is no statement)
				statementStart = true;
				fPos++;
				continue;
			}

			if (IsIdentifierChar(c)) {
				size_t start = fPos;
				while (fPos < fLength && IsIdentifierChar(fText[fPos]))
					fPos++;
				std::string word = fText.substr(start, fPos - start);

				// a prefix of a string (r"..", b'..', f"..") belongs to it
				if (fPos < fLength && (fText[fPos] == '"' || fText[fPos] == '\'') && word.size() <= 3
						&& word.find_first_not_of("rRbBuUfF") == std::string::npos) {
					SkipString();
					statementStart = false;
					continue;
				}

				if (statementStart && depth == 0 && word == "import")
					ReadImport();
				else if (statementStart && depth == 0 && word == "from")
					ReadFrom();
				statementStart = false;
				continue;
			}

			statementStart = false;
			fPos++;
		}
	}

private:
	bool SkipContinuation()
	{
		if (fText[fPos] != '\\')
			return false;
		if (fPos + 1 < fLength && fText[fPos + 1] == '\n') {
			fPos += 2;
			fLine++;
			return true;
		}
		if (fPos + 2 < fLength && fText[fPos + 1] == '\r' && fText[fPos + 2] == '\n') {
			fPos += 3;
			fLine++;
			return true;
		}
		return false;
	}

	/** blanks, and line breaks too when allowed (inside brackets of a from-import) */
	void SkipSpace(bool lineBreaks)
	{
		while (fPos < fLength) {
			if (IsBlank(fText[fPos])) {
				fPos++;
			} else if (SkipContinuation()) {
				continue;
			} else if (lineBreaks && fText[fPos] == '\n') {
				fLine++;
				fPos++;
			} else if (lineBreaks && fText[fPos] == '#') {
				while (fPos < fLength && fText[fPos] != '\n')
					fPos++;
			} else {
				break;
			}
		}
	}

	/** at a quote: skips the string, single or triple quoted, with escapes; counts the lines */
	void SkipString()
	{
		const char quote = fText[fPos];
		const bool triple = fPos + 2 < fLength && fText[fPos + 1] == quote && fText[fPos + 2] == quote;
		fPos += triple ? 3 : 1;
		while (fPos < fLength) {
			char c = fText[fPos];
			if (c == '\\' && fPos + 1 < fLength) {
				if (fText[fPos + 1] == '\n')
					fLine++;
				fPos += 2;
				continue;
			}
			if (c == '\n') {
				fLine++;
				if (!triple) {
					fPos++;		// an unterminated string ends with the line
					return;
				}
			} else if (c == quote) {
				if (!triple) {
					fPos++;
					return;
				}
				if (fPos + 2 < fLength && fText[fPos + 1] == quote && fText[fPos + 2] == quote) {
					fPos += 3;
					return;
				}
			}
			fPos++;
		}
	}

	std::string ReadWord()
	{
		size_t start = fPos;
		while (fPos < fLength && IsIdentifierChar(fText[fPos]))
			fPos++;
		return fText.substr(start, fPos - start);
	}

	/** a dotted name `a.b.c`, with the dots in front of a relative one */
	std::string ReadDottedName()
	{
		size_t start = fPos;
		while (fPos < fLength && (IsIdentifierChar(fText[fPos]) || fText[fPos] == '.'))
			fPos++;
		return fText.substr(start, fPos - start);
	}

	void Add(const std::string& name, unsigned line)
	{
		if (name.empty() || name.find_first_not_of('.') == std::string::npos)
			return;
		// relative imports belong to the project; the others are looked up
		fOut->push_back({name, std::string(), name[0] != '.', line});
	}

	/** after `import`: `a.b, c as d` */
	void ReadImport()
	{
		const unsigned line = fLine;
		for (;;) {
			SkipSpace(false);
			std::string name = ReadDottedName();
			if (name.empty())
				return;
			Add(name, line);
			SkipSpace(false);
			size_t save = fPos;
			if (ReadWord() == "as") {
				SkipSpace(false);
				ReadWord();
				SkipSpace(false);
			} else {
				fPos = save;
			}
			if (fPos < fLength && fText[fPos] == ',')
				fPos++;
			else
				return;
		}
	}

	/** after `from`: `a.b import c, d`, `. import x`, `..pkg import (y,\n z)` */
	void ReadFrom()
	{
		const unsigned line = fLine;
		SkipSpace(false);
		const std::string module = ReadDottedName();
		SkipSpace(false);
		size_t save = fPos;
		if (module.empty() || ReadWord() != "import") {
			fPos = save;
			return;
		}
		SkipSpace(false);
		const bool brackets = fPos < fLength && fText[fPos] == '(';
		if (brackets)
			fPos++;

		const bool onlyDots = module.find_first_not_of('.') == std::string::npos;
		bool any = false;
		for (;;) {
			SkipSpace(brackets);
			if (fPos < fLength && fText[fPos] == '*') {
				fPos++;
				break;
			}
			std::string name = ReadWord();
			if (name.empty())
				break;
			// `from . import x`: the dots alone are the package, x is the module that is meant
			if (onlyDots)
				Add(module + name, line);
			any = true;
			SkipSpace(brackets);
			size_t save = fPos;
			if (ReadWord() == "as") {
				SkipSpace(brackets);
				ReadWord();
				SkipSpace(brackets);
			} else {
				fPos = save;
			}
			if (fPos < fLength && fText[fPos] == ',')
				fPos++;
			else
				break;
		}
		if (brackets) {
			SkipSpace(true);
			if (fPos < fLength && fText[fPos] == ')')
				fPos++;
		}
		if (!onlyDots)
			Add(module, line);
		else if (!any)
			Add(module + "__init__", line);
	}

	const std::string&		fText;
	std::vector<Dependency>*	fOut;
	size_t					fLength;
	size_t					fPos = 0;
	unsigned				fLine = 1;
};


bool IsFolder(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}


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


/** a.b.c in a folder: a/b/c.py, a/b/c.pyi or the package a/b/c/__init__.py */
std::string FindModule(const std::string& folder, const std::string& dotted)
{
	std::string relative = dotted;
	std::replace(relative.begin(), relative.end(), '.', '/');
	for (const char* candidate : {".py", ".pyi", "/__init__.py", "/__init__.pyi"}) {
		std::string path = folder + "/" + relative + candidate;
		if (IsFile(path)) {
			BPath normalized(path.c_str(), NULL, true);
			return normalized.Path() != NULL ? normalized.Path() : path;
		}
	}
	return std::string();
}

}	// namespace


const char*
PythonScanner::Name() const
{
	return "Python";
}


const std::vector<std::string>&
PythonScanner::Extensions() const
{
	static const std::vector<std::string> extensions = {"py", "pyw", "pyi"};
	return extensions;
}


void
PythonScanner::Scan(const std::string& text, std::vector<Dependency>* dependencies) const
{
	Reader reader(text, dependencies);
	reader.Run();
}


const std::vector<std::string>&
PythonScanner::LibraryFolders() const
{
	if (fLibraryFoldersKnown)
		return fLibraryFolders;
	fLibraryFoldersKnown = true;

	// the folders where Python looks for modules: lib/python3.x and its site-packages, user before system, not packaged before packaged
	const directory_which places[] = {B_USER_NONPACKAGED_LIB_DIRECTORY, B_USER_LIB_DIRECTORY, B_SYSTEM_NONPACKAGED_LIB_DIRECTORY,
		B_SYSTEM_LIB_DIRECTORY};
	for (directory_which place : places) {
		BPath libraries;
		if (find_directory(place, &libraries) != B_OK)
			continue;

		std::vector<std::string> versions;
		if (DIR* dir = opendir(libraries.Path())) {
			while (struct dirent* entry = readdir(dir)) {
				if (strncmp(entry->d_name, "python3", 7) == 0 && IsFolder(std::string(libraries.Path()) + "/" + entry->d_name))
					versions.push_back(entry->d_name);
			}
			closedir(dir);
		}
		// the newest version first
		std::sort(versions.begin(), versions.end(), [](const std::string& a, const std::string& b) {
			return a.size() != b.size() ? a.size() > b.size() : a > b;
		});
		for (const std::string& version : versions) {
			std::string base = std::string(libraries.Path()) + "/" + version;
			for (const std::string& folder : {base + "/site-packages", base})
				fLibraryFolders.push_back(folder);
		}
	}
	return fLibraryFolders;
}


void
PythonScanner::Resolve(Dependency* dependency, const std::string& sourcePath) const
{
	const std::string& name = dependency->name;
	const std::string sourceFolder = Parent(sourcePath);

	// relative: one dot is the folder of the file, each further dot one up
	if (!name.empty() && name[0] == '.') {
		size_t dots = name.find_first_not_of('.');
		if (dots == std::string::npos)
			return;
		std::string folder = sourceFolder;
		for (size_t i = 1; i < dots; i++)
			folder = Parent(folder);
		dependency->resolved = FindModule(folder, name.substr(dots));
		return;
	}

	// the project: the folder of the file, and the folder above its package (where `import package.module` starts)
	std::vector<std::string> folders;
	folders.push_back(sourceFolder);
	std::string root = sourceFolder;
	while (IsFile(root + "/__init__.py") && Parent(root) != root)
		root = Parent(root);
	if (root != sourceFolder)
		folders.push_back(root);

	for (const std::string& folder : folders) {
		dependency->resolved = FindModule(folder, name);
		if (!dependency->resolved.empty()) {
			dependency->global = false;
			return;
		}
	}
	for (const std::string& folder : LibraryFolders()) {
		dependency->resolved = FindModule(folder, name);
		if (!dependency->resolved.empty())
			return;
	}
}
