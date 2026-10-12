/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "RustScanner.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstring>

#include <Path.h>


namespace {

bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }
bool IsIdentifierStart(char c) { return isalpha((unsigned char) c) || c == '_' || (unsigned char) c >= 0x80; }
bool IsIdentifierChar(char c) { return isalnum((unsigned char) c) || c == '_' || (unsigned char) c >= 0x80; }


/** reads the Rust source: the modules and the use declarations, the rest is skipped */
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
			if (c == '"') {
				SkipString();
				continue;
			}
			if (c == '\'') {
				SkipQuote();
				continue;
			}
			if (c == '#' && fPos + 1 < fLength && (fText[fPos + 1] == '[' || (fText[fPos + 1] == '!' && fPos + 2 < fLength && fText[fPos + 2] == '['))) {
				ReadAttribute();
				continue;
			}
			if (c == '{') {
				fDepth++;
				fPos++;
				continue;
			}
			if (c == '}') {
				if (fDepth > 0)
					fDepth--;
				while (!fModules.empty() && fModules.back().depth > fDepth)
					fModules.pop_back();
				fPos++;
				continue;
			}
			if (IsIdentifierStart(c)) {
				ReadWord();
				continue;
			}
			fPos++;
		}
	}

private:
	struct Module {
		std::string	name;
		int			depth;		///< the depth inside of its braces
	};

	/** a comment, nested block comments too: skips it and returns true */
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
			int level = 1;
			fPos += 2;
			while (fPos < fLength && level > 0) {
				if (fText[fPos] == '/' && fPos + 1 < fLength && fText[fPos + 1] == '*') {
					level++;
					fPos += 2;
				} else if (fText[fPos] == '*' && fPos + 1 < fLength && fText[fPos + 1] == '/') {
					level--;
					fPos += 2;
				} else {
					if (fText[fPos] == '\n')
						fLine++;
					fPos++;
				}
			}
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

	/** at the opening quote of a string (a prefix like b or c is already read): strings can span lines, `\` escapes */
	std::string SkipString()
	{
		fPos++;
		size_t start = fPos;
		while (fPos < fLength && fText[fPos] != '"') {
			if (fText[fPos] == '\\' && fPos + 1 < fLength) {
				if (fText[fPos + 1] == '\n')
					fLine++;
				fPos++;
			}
			if (fText[fPos] == '\n')
				fLine++;
			fPos++;
		}
		std::string content = fText.substr(start, fPos - start);
		if (fPos < fLength)
			fPos++;
		return content;
	}

	/** at `#` or `"` of a raw string r"..." / r#"..."# (hashes already counted by the caller at the first `"`) */
	void SkipRawString(size_t hashes)
	{
		// fPos is at the opening quote
		fPos++;
		const std::string closing = "\"" + std::string(hashes, '#');
		size_t end = fText.find(closing, fPos);
		size_t stop = end == std::string::npos ? fLength : end + closing.size();
		for (size_t i = fPos; i < stop; i++) {
			if (fText[i] == '\n')
				fLine++;
		}
		fPos = stop;
	}

	/** at a `'`: a character literal ('x', '\\n', '\\u{1F600}') or a lifetime or label ('a) */
	void SkipQuote()
	{
		if (fPos + 1 >= fLength) {
			fPos++;
			return;
		}
		char next = fText[fPos + 1];
		if (next == '\\') {
			// an escape: up to the closing quote
			fPos += 2;
			while (fPos < fLength && fText[fPos] != '\'' && fText[fPos] != '\n')
				fPos++;
			if (fPos < fLength && fText[fPos] == '\'')
				fPos++;
			return;
		}
		// one character (it can be several bytes) and the closing quote
		size_t length = 1;
		unsigned char first = (unsigned char) next;
		if (first >= 0xF0)
			length = 4;
		else if (first >= 0xE0)
			length = 3;
		else if (first >= 0xC0)
			length = 2;
		if (fPos + 1 + length < fLength && fText[fPos + 1 + length] == '\'') {
			fPos += length + 2;
			return;
		}
		fPos++;		// a lifetime: the name that follows is read as a word
	}

	/** `#[...]` and `#![...]`: the attribute `path = "x.rs"` is remembered for the next `mod` */
	void ReadAttribute()
	{
		fPos++;
		if (fText[fPos] == '!')
			fPos++;
		fPos++;		// [
		int level = 1;
		size_t start = fPos;
		std::string path;
		bool isPath = false;
		SkipSpace();
		if (fText.compare(fPos, 4, "path") == 0 && (fPos + 4 >= fLength || !IsIdentifierChar(fText[fPos + 4])))
			isPath = true;
		fPos = start;
		while (fPos < fLength && level > 0) {
			char c = fText[fPos];
			if (c == '"') {
				std::string content = SkipString();
				if (isPath && path.empty())
					path = content;
				continue;
			}
			if (c == '[')
				level++;
			else if (c == ']')
				level--;
			else if (c == '\n')
				fLine++;
			fPos++;
		}
		if (isPath)
			fPendingPath = path;
	}

	std::string ReadIdentifier()
	{
		size_t start = fPos;
		while (fPos < fLength && IsIdentifierChar(fText[fPos]))
			fPos++;
		return fText.substr(start, fPos - start);
	}

	void ReadWord()
	{
		const size_t start = fPos;
		const unsigned line = fLine;
		std::string word = ReadIdentifier();

		// prefixes of strings: r"", r#""#, b"", br"", c"", and raw identifiers r#name
		if ((word == "r" || word == "br" || word == "cr") && fPos < fLength && (fText[fPos] == '"' || fText[fPos] == '#')) {
			size_t hashes = 0;
			size_t at = fPos;
			while (at < fLength && fText[at] == '#') {
				hashes++;
				at++;
			}
			if (at < fLength && fText[at] == '"') {
				fPos = at;
				SkipRawString(hashes);
				return;
			}
			if (word == "r" && hashes == 1 && at < fLength && IsIdentifierStart(fText[at])) {
				fPos = at;
				ReadIdentifier();		// a raw identifier (r#mod): not a keyword
				return;
			}
		}
		if ((word == "b" || word == "c") && fPos < fLength) {
			if (fText[fPos] == '"') {
				SkipString();
				return;
			}
			if (fText[fPos] == '\'' && word == "b") {
				SkipQuote();
				return;
			}
		}
		// a name after a path or a dot is no keyword
		if (start > 0 && (fText[start - 1] == '.' || (start > 1 && fText[start - 1] == ':' && fText[start - 2] == ':')))
			return;

		if (word == "mod")
			ReadMod(line);
		else if (word == "use")
			ReadUse(line);
		else if (word == "extern")
			ReadExtern(line);
	}

	void ReadMod(unsigned line)
	{
		SkipSpace();
		std::string name = ReadIdentifier();
		if (name.empty()) {
			fPendingPath.clear();
			return;
		}
		SkipSpace();
		if (fPos < fLength && fText[fPos] == ';') {
			fPos++;
			std::string reference;
			if (!fPendingPath.empty()) {
				reference = fPendingPath;		// the path as written
			} else {
				reference = "self";
				for (const Module& module : fModules)
					reference += "::" + module.name;
				reference += "::" + name;
			}
			// a module of the crate
			fOut->push_back({reference, std::string(), false, line});
		} else if (fPos < fLength && fText[fPos] == '{') {
			// an inline module: the modules below it are in its folder
			fPos++;
			fDepth++;
			fModules.push_back({name, fDepth});
		}
		fPendingPath.clear();
	}

	void ReadExtern(unsigned line)
	{
		SkipSpace();
		size_t save = fPos;
		if (ReadIdentifier() != "crate") {
			fPos = save;		// extern "C" { ... } and the like
			return;
		}
		SkipSpace();
		std::string name = ReadIdentifier();
		if (!name.empty() && name != "self")
			fOut->push_back({name, std::string(), true, line});
	}

	void ReadUse(unsigned line)
	{
		std::vector<std::string> paths;
		ParseTree("", false, &paths);
		SkipSpace();
		while (fPos < fLength && fText[fPos] != ';' && fText[fPos] != '\n')
			fPos++;
		for (const std::string& path : paths) {
			if (path.empty())
				continue;
			bool local = path == "crate" || path == "self" || path == "super" || path.compare(0, 7, "crate::") == 0
				|| path.compare(0, 6, "self::") == 0 || path.compare(0, 7, "super::") == 0;
			fOut->push_back({path, std::string(), !local, line});
		}
	}

	bool AtPathSeparator()
	{
		return fPos + 1 < fLength && fText[fPos] == ':' && fText[fPos + 1] == ':';
	}

	/** a use tree: `a::b::c`, `a::{b, c::d}`, `a::*`, with `as` renames; the paths go to `out` */
	void ParseTree(const std::string& prefix, bool inGroup, std::vector<std::string>* out)
	{
		SkipSpace();
		std::string path = prefix;
		if (AtPathSeparator())
			fPos += 2;
		for (;;) {
			SkipSpace();
			if (fPos >= fLength)
				return;
			char c = fText[fPos];
			if (c == '{') {
				fPos++;
				for (;;) {
					SkipSpace();
					if (fPos < fLength && fText[fPos] == '}') {
						fPos++;
						break;
					}
					size_t before = fPos;
					ParseTree(path, true, out);
					SkipSpace();
					if (fPos < fLength && fText[fPos] == ',') {
						fPos++;
					} else if (fPos < fLength && fText[fPos] == '}') {
						fPos++;
						break;
					} else if (fPos == before) {
						break;
					} else {
						break;
					}
				}
				return;
			}
			if (c == '*') {
				fPos++;
				out->push_back(path);
				return;
			}
			if (!IsIdentifierStart(c))
				return;

			std::string segment = ReadIdentifier();
			if (segment == "self" && inGroup && !AtPathSeparator()) {
				// `use a::{self}`: the path of the group
			} else {
				path = path.empty() ? segment : path + "::" + segment;
			}
			SkipSpace();
			if (AtPathSeparator()) {
				fPos += 2;
				continue;
			}
			// a rename: `as name`
			size_t save = fPos;
			if (ReadIdentifier() == "as") {
				SkipSpace();
				ReadIdentifier();
			} else {
				fPos = save;
			}
			out->push_back(path);
			return;
		}
	}

	const std::string&		fText;
	std::vector<Dependency>*	fOut;
	size_t					fLength;
	size_t					fPos = 0;
	unsigned				fLine = 1;
	int						fDepth = 0;
	std::vector<Module>		fModules;
	std::string				fPendingPath;	///< from #[path = "..."], for the next mod
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


std::string Leaf(const std::string& path)
{
	size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}


std::string Normalized(const std::string& path)
{
	BPath normalized(path.c_str(), NULL, true);
	return normalized.Path() != NULL ? normalized.Path() : path;
}


std::vector<std::string> Segments(const std::string& path)
{
	std::vector<std::string> segments;
	size_t start = 0;
	while (start <= path.size()) {
		size_t end = path.find("::", start);
		if (end == std::string::npos)
			end = path.size();
		if (end > start)
			segments.push_back(path.substr(start, end - start));
		start = end + 2;
	}
	return segments;
}


/**
 * Follows the modules of a path from a folder: `a::b` is a.rs or a/mod.rs, then in a/ b.rs or b/mod.rs. Where the path goes on into things that
 * are not modules, the last module counts. @return the file of the last module, empty if there is none.
 */
std::string FollowModules(std::string folder, const std::vector<std::string>& segments, size_t from)
{
	std::string found;
	for (size_t i = from; i < segments.size(); i++) {
		const std::string& segment = segments[i];
		if (IsFile(folder + "/" + segment + ".rs")) {
			found = folder + "/" + segment + ".rs";
		} else if (IsFile(folder + "/" + segment + "/mod.rs")) {
			found = folder + "/" + segment + "/mod.rs";
		} else if (!IsFolder(folder + "/" + segment)) {
			break;		// an item, not a module (an inline module has a folder, but no file)
		}
		folder += "/" + segment;
	}
	return found;
}

}	// namespace


const char*
RustScanner::Name() const
{
	return "Rust";
}


const std::vector<FileKind>&
RustScanner::FileKinds() const
{
	static const std::vector<FileKind> kinds = {{"rs", "text/x-rust"}};
	return kinds;
}


void
RustScanner::Scan(const std::string& text, std::vector<Dependency>* dependencies) const
{
	Reader reader(text, dependencies);
	reader.Run();
}


void
RustScanner::Resolve(Dependency* dependency, const std::string& sourcePath) const
{
	const std::string& name = dependency->name;
	const std::string sourceFolder = Parent(sourcePath);
	const std::string leaf = Leaf(sourcePath);

	// #[path = "x.rs"]: relative to the folder of the file
	if (name.find("::") == std::string::npos && (name.find('/') != std::string::npos || (name.size() > 3
			&& name.compare(name.size() - 3, 3, ".rs") == 0))) {
		if (IsFile(sourceFolder + "/" + name))
			dependency->resolved = Normalized(sourceFolder + "/" + name);
		return;
	}

	// the folder of the module of the file: its own folder for main.rs, lib.rs, mod.rs, else a folder with the name of the file
	const bool isRoot = leaf == "main.rs" || leaf == "lib.rs" || leaf == "mod.rs";
	const std::string moduleFolder = isRoot ? sourceFolder : sourceFolder + "/" + leaf.substr(0, leaf.size() - 3);

	// the source folder of the crate: the folder of lib.rs or main.rs, above the file
	std::string crateFolder = sourceFolder;
	for (int level = 0; level < 32; level++) {
		if (IsFile(crateFolder + "/lib.rs") || IsFile(crateFolder + "/main.rs"))
			break;
		std::string parent = Parent(crateFolder);
		if (parent == crateFolder) {
			crateFolder = sourceFolder;
			break;
		}
		crateFolder = parent;
	}

	const std::vector<std::string> segments = Segments(name);
	if (segments.empty())
		return;

	std::string found;
	if (segments[0] == "crate") {
		found = FollowModules(crateFolder, segments, 1);
	} else if (segments[0] == "self") {
		found = FollowModules(moduleFolder, segments, 1);
	} else if (segments[0] == "super") {
		std::string folder = moduleFolder;
		size_t next = 0;
		while (next < segments.size() && segments[next] == "super") {
			folder = Parent(folder);
			next++;
		}
		found = FollowModules(folder, segments, next);
	} else {
		// a name from the module of the file, else from the crate root; anything else is another crate
		found = FollowModules(moduleFolder, segments, 0);
		if (found.empty())
			found = FollowModules(crateFolder, segments, 0);
		if (!found.empty())
			dependency->global = false;
	}

	if (!found.empty())
		dependency->resolved = Normalized(found);
}
