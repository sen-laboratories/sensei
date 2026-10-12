/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "JavaScriptScanner.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#include <FindDirectory.h>
#include <Path.h>


namespace {

bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }
bool IsIdentifierStart(char c) { return isalpha((unsigned char) c) || c == '_' || c == '$' || (unsigned char) c >= 0x80; }
bool IsIdentifierChar(char c) { return isalnum((unsigned char) c) || c == '_' || c == '$' || (unsigned char) c >= 0x80; }


/** reads the source: import, export-from, require and the TypeScript reference directives; the rest is skipped */
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
			if (SkipComment())
				continue;
			if (c == '"' || c == '\'') {
				SkipString();
				fRegexAllowed = false;
				continue;
			}
			if (c == '`') {
				SkipTemplate();
				fRegexAllowed = false;
				continue;
			}
			if (c == '/' && fRegexAllowed) {
				SkipRegex();
				fRegexAllowed = false;
				continue;
			}
			if (IsIdentifierStart(c)) {
				ReadWord();
				continue;
			}
			if (isdigit((unsigned char) c)) {
				while (fPos < fLength && (IsIdentifierChar(fText[fPos]) || fText[fPos] == '.'))
					fPos++;
				fRegexAllowed = false;
				continue;
			}
			// punctuation: after a closing bracket a slash divides, else it starts a regular expression
			fRegexAllowed = !(c == ')' || c == ']' || c == '}');
			fPrevDot = c == '.';
			fPos++;
		}
	}

private:
	/** a comment: skips it and returns true; `/// <reference .../>` directives are read */
	bool SkipComment()
	{
		if (fText[fPos] != '/' || fPos + 1 >= fLength)
			return false;
		if (fText[fPos + 1] == '/') {
			size_t start = fPos;
			while (fPos < fLength && fText[fPos] != '\n')
				fPos++;
			if (fText.compare(start, 3, "///") == 0)
				ReadReferenceDirective(fText.substr(start, fPos - start));
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

	/** `/// <reference path="x" />`, `types="x"` and `lib="x"` (the last is no file) */
	void ReadReferenceDirective(const std::string& line)
	{
		if (line.find("<reference") == std::string::npos)
			return;
		for (const char* attribute : {"path", "types"}) {
			size_t at = line.find(std::string(attribute) + "=");
			if (at == std::string::npos)
				continue;
			size_t open = at + strlen(attribute) + 1;
			if (open >= line.size() || (line[open] != '"' && line[open] != '\''))
				continue;
			size_t close = line.find(line[open], open + 1);
			if (close == std::string::npos)
				continue;
			std::string name = line.substr(open + 1, close - open - 1);
			if (!name.empty())
				fOut->push_back({name, std::string(), !IsRelative(name), fLine});
		}
	}

	static bool IsRelative(const std::string& name)
	{
		return name[0] == '.' || name[0] == '/';
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

	/** a string with ' or ": returns its content (escapes are not translated); a string ends with its line */
	std::string SkipString()
	{
		const char quote = fText[fPos++];
		size_t start = fPos;
		while (fPos < fLength && fText[fPos] != quote && fText[fPos] != '\n') {
			if (fText[fPos] == '\\' && fPos + 1 < fLength && fText[fPos + 1] != '\n')
				fPos++;
			fPos++;
		}
		std::string content = fText.substr(start, fPos - start);
		if (fPos < fLength && fText[fPos] == quote)
			fPos++;
		return content;
	}

	/** a template literal, with the expressions in `${ ... }` (which can hold strings and templates) */
	void SkipTemplate()
	{
		fPos++;
		while (fPos < fLength && fText[fPos] != '`') {
			char c = fText[fPos];
			if (c == '\\' && fPos + 1 < fLength) {
				if (fText[fPos + 1] == '\n')
					fLine++;
				fPos += 2;
				continue;
			}
			if (c == '\n')
				fLine++;
			if (c == '$' && fPos + 1 < fLength && fText[fPos + 1] == '{') {
				fPos += 2;
				SkipExpression();
				continue;
			}
			fPos++;
		}
		if (fPos < fLength)
			fPos++;
	}

	/** after `${`: up to the matching `}` */
	void SkipExpression()
	{
		int depth = 1;
		while (fPos < fLength && depth > 0) {
			char c = fText[fPos];
			if (c == '\n') {
				fLine++;
				fPos++;
			} else if (c == '"' || c == '\'') {
				SkipString();
			} else if (c == '`') {
				SkipTemplate();
			} else if (SkipComment()) {
				continue;
			} else {
				if (c == '{')
					depth++;
				else if (c == '}')
					depth--;
				fPos++;
			}
		}
	}

	/** a regular expression literal: up to the closing slash that is not in a class [...] */
	void SkipRegex()
	{
		fPos++;
		bool inClass = false;
		while (fPos < fLength && fText[fPos] != '\n') {
			char c = fText[fPos];
			if (c == '\\' && fPos + 1 < fLength) {
				fPos += 2;
				continue;
			}
			if (c == '[')
				inClass = true;
			else if (c == ']')
				inClass = false;
			else if (c == '/' && !inClass) {
				fPos++;
				return;
			}
			fPos++;
		}
	}

	std::string ReadIdentifier()
	{
		size_t start = fPos;
		while (fPos < fLength && IsIdentifierChar(fText[fPos]))
			fPos++;
		return fText.substr(start, fPos - start);
	}

	void Add(const std::string& name, unsigned line)
	{
		if (!name.empty())
			fOut->push_back({name, std::string(), !IsRelative(name), line});
	}

	void ReadWord()
	{
		const unsigned line = fLine;
		const bool afterDot = fPrevDot;
		std::string word = ReadIdentifier();
		fPrevDot = false;

		// words after which an expression (and so a regular expression) starts
		static const char* const kOperators[] = {"return", "typeof", "instanceof", "in", "of", "new", "delete", "void", "throw", "case", "do",
			"else", "yield", "await"};
		fRegexAllowed = false;
		for (const char* op : kOperators) {
			if (word == op) {
				fRegexAllowed = true;
				break;
			}
		}
		if (afterDot)
			return;		// a property: `.import`, `.require`

		if (word == "import")
			ReadImport(line);
		else if (word == "export")
			ReadExport(line);
		else if (word == "require")
			ReadRequire(line);
	}

	/** `'x'` at the position (after blanks): adds it */
	bool ReadSpecifier(unsigned line)
	{
		SkipSpace();
		if (fPos < fLength && (fText[fPos] == '"' || fText[fPos] == '\'')) {
			Add(SkipString(), line);
			return true;
		}
		return false;
	}

	/** require('x') */
	void ReadRequire(unsigned line)
	{
		SkipSpace();
		if (fPos < fLength && fText[fPos] == '(') {
			fPos++;
			ReadSpecifier(line);
		}
	}

	/** the clause of an import or export up to `from 'x'`: names, `{ a as b }`, `* as ns`, and for TypeScript `x = require('y')` */
	void ReadFromClause(unsigned line)
	{
		const size_t limit = std::min(fLength, fPos + 8000);
		while (fPos < limit) {
			SkipSpace();
			if (fPos >= fLength)
				return;
			char c = fText[fPos];
			if (c == '{') {
				// the names: up to the closing brace
				int depth = 0;
				while (fPos < fLength) {
					if (fText[fPos] == '{') {
						depth++;
					} else if (fText[fPos] == '}') {
						depth--;
						if (depth == 0) {
							fPos++;
							break;
						}
					} else if (fText[fPos] == '\n') {
						fLine++;
					} else if (fText[fPos] == '"' || fText[fPos] == '\'') {
						SkipString();
						continue;
					} else if (SkipComment()) {
						continue;
					}
					fPos++;
				}
			} else if (c == '*' || c == ',') {
				fPos++;
			} else if (c == '=') {
				fPos++;
				SkipSpace();
				if (ReadIdentifier() == "require") {
					SkipSpace();
					if (fPos < fLength && fText[fPos] == '(') {
						fPos++;
						ReadSpecifier(line);
					}
				}
				return;
			} else if (IsIdentifierStart(c)) {
				std::string word = ReadIdentifier();
				if (word == "from") {
					if (ReadSpecifier(line))
						return;
				}
			} else {
				return;		// `;` or anything else: not an import clause
			}
		}
	}

	void ReadImport(unsigned line)
	{
		SkipSpace();
		if (fPos >= fLength)
			return;
		char c = fText[fPos];
		if (c == '"' || c == '\'') {
			Add(SkipString(), line);			// import 'x'
		} else if (c == '(') {
			fPos++;
			ReadSpecifier(line);				// import('x')
		} else if (c == '.') {
			return;								// import.meta
		} else {
			ReadFromClause(line);
		}
	}

	void ReadExport(unsigned line)
	{
		SkipSpace();
		if (fPos >= fLength)
			return;
		// export type { X } from 'y'
		size_t save = fPos;
		if (IsIdentifierStart(fText[fPos]) && ReadIdentifier() != "type")
			fPos = save;
		SkipSpace();
		if (fPos < fLength && (fText[fPos] == '{' || fText[fPos] == '*'))
			ReadFromClause(line);
	}

	const std::string&		fText;
	std::vector<Dependency>*	fOut;
	size_t					fLength;
	size_t					fPos = 0;
	unsigned				fLine = 1;
	bool					fRegexAllowed = true;
	bool					fPrevDot = false;
};


bool IsFile(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}


bool IsFolder(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}


std::string Parent(const std::string& path)
{
	size_t slash = path.rfind('/');
	if (slash == std::string::npos)
		return ".";
	return slash == 0 ? "/" : path.substr(0, slash);
}


std::string Normalized(const std::string& path)
{
	BPath normalized(path.c_str(), NULL, true);
	return normalized.Path() != NULL ? normalized.Path() : path;
}


bool EndsWith(const std::string& text, const char* suffix)
{
	size_t length = strlen(suffix);
	return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}


/** the value of a string property in a JSON text: `"main": "lib/index.js"` (nothing more than that is read) */
std::string JsonString(const std::string& json, const std::string& key)
{
	size_t at = json.find("\"" + key + "\"");
	if (at == std::string::npos)
		return std::string();
	size_t colon = json.find(':', at + key.size() + 2);
	if (colon == std::string::npos)
		return std::string();
	size_t open = json.find_first_not_of(" \t\r\n", colon + 1);
	if (open == std::string::npos || json[open] != '"')
		return std::string();
	size_t close = json.find('"', open + 1);
	return close == std::string::npos ? std::string() : json.substr(open + 1, close - open - 1);
}


/** the file of a module name in a folder, as Node and TypeScript find it; empty if there is none */
std::string FindModuleFile(const std::string& base, bool typescript)
{
	const std::vector<std::string> extensions = typescript
		? std::vector<std::string>{".ts", ".tsx", ".d.ts", ".js", ".jsx", ".mjs", ".cjs", ".json"}
		: std::vector<std::string>{".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx", ".d.ts", ".json"};

	if (IsFile(base))
		return base;
	// TypeScript imports name the compiled file: x.js is x.ts
	for (const char* js : {".js", ".jsx", ".mjs", ".cjs"}) {
		if (EndsWith(base, js)) {
			std::string stem = base.substr(0, base.size() - strlen(js));
			for (const char* ts : {".ts", ".tsx", ".mts", ".cts"}) {
				if (IsFile(stem + ts))
					return stem + ts;
			}
		}
	}
	for (const std::string& extension : extensions) {
		if (IsFile(base + extension))
			return base + extension;
	}

	if (IsFolder(base)) {
		std::ifstream file(base + "/package.json");
		std::stringstream buffer;
		buffer << file.rdbuf();
		for (const char* key : {"types", "typings", "module", "main"}) {
			std::string entry = JsonString(buffer.str(), key);
			if (!entry.empty()) {
				std::string path = base + "/" + entry;
				std::string found = IsFile(path) ? path : std::string();
				for (size_t i = 0; found.empty() && i < extensions.size(); i++) {
					if (IsFile(path + extensions[i]))
						found = path + extensions[i];
				}
				if (found.empty() && IsFile(path + "/index.js"))
					found = path + "/index.js";
				if (!found.empty())
					return found;
			}
		}
		for (const std::string& extension : extensions) {
			if (IsFile(base + "/index" + extension))
				return base + "/index" + extension;
		}
	}
	return std::string();
}

}	// namespace


const char*
JavaScriptScanner::Name() const
{
	return "JavaScript/TypeScript";
}


const std::vector<FileKind>&
JavaScriptScanner::FileKinds() const
{
	static const std::vector<FileKind> kinds = {
		{"js", "text/javascript"}, {"mjs", "text/javascript"}, {"cjs", "text/javascript"}, {"jsx", "text/jsx"},
		{"ts", "text/x-typescript"}, {"mts", "text/x-typescript"}, {"cts", "text/x-typescript"}, {"tsx", "text/tsx"}
	};
	return kinds;
}


void
JavaScriptScanner::Scan(const std::string& text, std::vector<Dependency>* dependencies) const
{
	Reader reader(text, dependencies);
	reader.Run();
}


void
JavaScriptScanner::Resolve(Dependency* dependency, const std::string& sourcePath) const
{
	const std::string& name = dependency->name;
	const std::string sourceFolder = Parent(sourcePath);
	const bool typescript = EndsWith(sourcePath, ".ts") || EndsWith(sourcePath, ".tsx") || EndsWith(sourcePath, ".mts")
		|| EndsWith(sourcePath, ".cts");

	if (name.empty())
		return;

	// the project: a path
	if (name[0] == '.' || name[0] == '/') {
		std::string base = name[0] == '/' ? name : sourceFolder + "/" + name;
		std::string found = FindModuleFile(base, typescript);
		if (!found.empty())
			dependency->resolved = Normalized(found);
		return;
	}

	// a package: node_modules from the folder of the file up to the root, then the ones of the library folders of Haiku
	std::vector<std::string> folders;
	std::string folder = sourceFolder;
	for (int level = 0; level < 64; level++) {
		folders.push_back(folder + "/node_modules");
		std::string parent = Parent(folder);
		if (parent == folder)
			break;
		folder = parent;
	}
	const directory_which places[] = {B_USER_NONPACKAGED_LIB_DIRECTORY, B_USER_LIB_DIRECTORY, B_SYSTEM_NONPACKAGED_LIB_DIRECTORY,
		B_SYSTEM_LIB_DIRECTORY};
	for (directory_which place : places) {
		BPath libraries;
		if (find_directory(place, &libraries) == B_OK)
			folders.push_back(std::string(libraries.Path()) + "/node_modules");
	}
	for (const std::string& modules : folders) {
		std::string found = FindModuleFile(modules + "/" + name, typescript);
		if (found.empty() && typescript)
			found = FindModuleFile(modules + "/@types/" + name, typescript);
		if (!found.empty()) {
			dependency->resolved = Normalized(found);
			return;
		}
	}
}
