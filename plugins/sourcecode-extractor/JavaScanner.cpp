/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "JavaScanner.h"

#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <sstream>


namespace {

bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }
bool IsIdentifierChar(char c) { return isalnum((unsigned char) c) || c == '_' || c == '$' || (unsigned char) c >= 0x80; }


/** reads the Java source: the package declaration and the imports, the rest is skipped */
class Reader {
public:
	Reader(const std::string& text, std::vector<Dependency>* out, std::string* package)
		:
		fText(text),
		fOut(out),
		fPackage(package),
		fLength(text.size())
	{
	}

	void Run()
	{
		int depth = 0;			// braces: imports and the package are only at the top level
		bool statementStart = true;

		while (fPos < fLength) {
			char c = fText[fPos];

			if (c == '\n') {
				fLine++;
				fPos++;
				continue;
			}
			if (IsBlank(c)) {
				fPos++;
				continue;
			}
			if (c == '/' && fPos + 1 < fLength && fText[fPos + 1] == '/') {
				while (fPos < fLength && fText[fPos] != '\n')
					fPos++;
				continue;
			}
			if (c == '/' && fPos + 1 < fLength && fText[fPos + 1] == '*') {
				fPos += 2;
				while (fPos < fLength && !(fText[fPos] == '*' && fPos + 1 < fLength && fText[fPos + 1] == '/')) {
					if (fText[fPos] == '\n')
						fLine++;
					fPos++;
				}
				fPos = std::min(fLength, fPos + 2);
				continue;
			}
			if (c == '"') {
				SkipString();
				statementStart = false;
				continue;
			}
			if (c == '\'') {
				SkipCharacter();
				statementStart = false;
				continue;
			}
			if (c == '{') {
				depth++;
			} else if (c == '}' && depth > 0) {
				depth--;
			}
			if (c == ';' || c == '{' || c == '}') {
				statementStart = true;
				fPos++;
				continue;
			}

			if (IsIdentifierChar(c)) {
				size_t start = fPos;
				while (fPos < fLength && IsIdentifierChar(fText[fPos]))
					fPos++;
				std::string word = fText.substr(start, fPos - start);
				if (statementStart && depth == 0) {
					if (word == "import")
						ReadImport();
					else if (word == "package")
						ReadPackage();
				}
				statementStart = false;
				continue;
			}

			statementStart = false;
			fPos++;
		}
	}

private:
	void SkipSpace()
	{
		while (fPos < fLength) {
			if (IsBlank(fText[fPos])) {
				fPos++;
			} else if (fText[fPos] == '\n') {
				fLine++;
				fPos++;
			} else if (fText[fPos] == '/' && fPos + 1 < fLength && fText[fPos + 1] == '*') {
				fPos += 2;
				while (fPos < fLength && !(fText[fPos] == '*' && fPos + 1 < fLength && fText[fPos + 1] == '/')) {
					if (fText[fPos] == '\n')
						fLine++;
					fPos++;
				}
				fPos = std::min(fLength, fPos + 2);
			} else if (fText[fPos] == '/' && fPos + 1 < fLength && fText[fPos + 1] == '/') {
				while (fPos < fLength && fText[fPos] != '\n')
					fPos++;
			} else {
				break;
			}
		}
	}

	/** at a quote: a string, or a text block (""" ... """) */
	void SkipString()
	{
		const bool block = fPos + 2 < fLength && fText[fPos + 1] == '"' && fText[fPos + 2] == '"';
		fPos += block ? 3 : 1;
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
				if (!block) {
					fPos++;
					return;
				}
			} else if (c == '"') {
				if (!block) {
					fPos++;
					return;
				}
				if (fPos + 2 < fLength && fText[fPos + 1] == '"' && fText[fPos + 2] == '"') {
					fPos += 3;
					return;
				}
			}
			fPos++;
		}
	}

	void SkipCharacter()
	{
		fPos++;
		while (fPos < fLength && fText[fPos] != '\'' && fText[fPos] != '\n') {
			if (fText[fPos] == '\\' && fPos + 1 < fLength && fText[fPos + 1] != '\n')
				fPos++;
			fPos++;
		}
		if (fPos < fLength && fText[fPos] == '\'')
			fPos++;
	}

	/** a dotted name, blanks (and comments) allowed around the dots, `*` at the end for a package import */
	std::string ReadQualifiedName()
	{
		std::string name;
		for (;;) {
			SkipSpace();
			size_t start = fPos;
			if (fPos < fLength && fText[fPos] == '*') {
				fPos++;
				name += "*";
				return name;
			}
			while (fPos < fLength && IsIdentifierChar(fText[fPos]))
				fPos++;
			if (fPos == start)
				return name;
			name += fText.substr(start, fPos - start);
			SkipSpace();
			if (fPos < fLength && fText[fPos] == '.') {
				fPos++;
				name += ".";
			} else {
				return name;
			}
		}
	}

	void ReadPackage()
	{
		*fPackage = ReadQualifiedName();
	}

	void ReadImport()
	{
		const unsigned line = fLine;
		SkipSpace();
		bool isStatic = false;
		size_t save = fPos;
		size_t start = fPos;
		while (fPos < fLength && IsIdentifierChar(fText[fPos]))
			fPos++;
		if (fText.compare(start, fPos - start, "static") == 0 && fPos > start)
			isStatic = true;
		else
			fPos = save;

		std::string name = ReadQualifiedName();
		if (name.empty() || name.back() == '.')
			return;
		// a static import names a member of a class: the class is what is referenced (a.b.C.member, a.b.C.*)
		if (isStatic) {
			size_t dot = name.rfind('.');
			if (dot != std::string::npos)
				name = name.substr(0, dot);
		}
		// not global: decided when the file is looked up (the classes of the libraries have no sources)
		fOut->push_back({name, std::string(), true, line});
	}

	const std::string&		fText;
	std::vector<Dependency>*	fOut;
	std::string*			fPackage;
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

}	// namespace


const char*
JavaScanner::Name() const
{
	return "Java";
}


const std::vector<std::string>&
JavaScanner::Extensions() const
{
	static const std::vector<std::string> extensions = {"java"};
	return extensions;
}


const std::vector<std::string>&
JavaScanner::SemanticTypes() const
{
	static const std::vector<std::string> types = {"text/x-java-source"};
	return types;
}


void
JavaScanner::Scan(const std::string& text, std::vector<Dependency>* dependencies) const
{
	std::string package;
	Reader reader(text, dependencies, &package);
	reader.Run();
}


void
JavaScanner::Resolve(Dependency* dependency, const std::string& sourcePath) const
{
	const std::string& name = dependency->name;
	// a whole package has no file
	if (name.empty() || name.back() == '*')
		return;

	std::string relative = name;
	std::replace(relative.begin(), relative.end(), '.', '/');
	relative += ".java";

	std::vector<std::string> roots;
	const std::string sourceFolder = Parent(sourcePath);

	// the root that the package declaration of the file leads to
	std::string package;
	{
		std::ifstream file(sourcePath);
		std::stringstream buffer;
		buffer << file.rdbuf();
		std::vector<Dependency> unused;
		Reader reader(buffer.str(), &unused, &package);
		reader.Run();
	}
	if (!package.empty()) {
		std::string packagePath = package;
		std::replace(packagePath.begin(), packagePath.end(), '.', '/');
		if (sourceFolder.size() > packagePath.size()
				&& sourceFolder.compare(sourceFolder.size() - packagePath.size(), packagePath.size(), packagePath) == 0)
			roots.push_back(sourceFolder.substr(0, sourceFolder.size() - packagePath.size() - 1));
	}

	// the usual layouts, for the folder of the file and the ones above it
	std::string folder = sourceFolder;
	for (int level = 0; level < 8; level++) {
		for (const char* sub : {"/src/main/java", "/src/test/java", "/src", ""})
			roots.push_back(folder + sub);
		std::string parent = Parent(folder);
		if (parent == folder)
			break;
		folder = parent;
	}

	for (const std::string& root : roots) {
		std::string path = root + "/" + relative;
		if (IsFile(path)) {
			dependency->resolved = FindFileInFolders({root}, relative);
			dependency->global = false;
			return;
		}
	}
}
