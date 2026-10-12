/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "CFamilyScanner.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <PathFinder.h>
#include <StringList.h>


namespace {

/** the part of a preprocessor that is needed to find out which includes are switched off */
class Conditions {
public:
	void If(bool constantFalse, bool constantTrue)
	{
		fStack.push_back({constantFalse ? kFalse : (constantTrue ? kTrue : kUnknown), false});
	}

	/** #else, #elif: the later branches of a condition that was constant */
	void Else()
	{
		if (!fStack.empty())
			fStack.back().later = true;
	}

	void End()
	{
		if (!fStack.empty())
			fStack.pop_back();
	}

	bool Active() const
	{
		for (const Branch& branch : fStack) {
			if ((branch.kind == kFalse && !branch.later) || (branch.kind == kTrue && branch.later))
				return false;
		}
		return true;
	}

private:
	enum Kind { kUnknown, kFalse, kTrue };
	struct Branch {
		Kind	kind;
		bool	later;
	};
	std::vector<Branch>	fStack;
};


bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }
bool IsIdentifier(char c) { return isalnum((unsigned char) c) || c == '_'; }


/** at a backslash that ends a line: the length of the continuation (backslash, optional CR, line feed), else 0 */
size_t Continuation(const std::string& text, size_t i)
{
	if (text[i] != '\\')
		return 0;
	if (i + 1 < text.size() && text[i + 1] == '\n')
		return 2;
	if (i + 2 < text.size() && text[i + 1] == '\r' && text[i + 2] == '\n')
		return 3;
	return 0;
}


/** skips blanks and continuations from i; counts the lines that are passed */
size_t SkipBlanks(const std::string& text, size_t i, unsigned* line)
{
	while (i < text.size()) {
		if (IsBlank(text[i])) {
			i++;
		} else if (size_t length = Continuation(text, i)) {
			i += length;
			(*line)++;
		} else {
			break;
		}
	}
	return i;
}

}	// namespace


const char*
CFamilyScanner::Name() const
{
	return "C/C++";
}


const std::vector<std::string>&
CFamilyScanner::Extensions() const
{
	static const std::vector<std::string> extensions = {"c", "h", "cc", "cpp", "cxx", "c++", "hh", "hpp", "hxx", "h++", "inl", "m", "mm"};
	return extensions;
}


void
CFamilyScanner::Scan(const std::string& text, std::vector<Dependency>* dependencies) const
{
	Conditions conditions;
	const size_t length = text.size();
	size_t i = 0;
	unsigned line = 1;
	bool atLineStart = true;	// only blanks and comments since the last line break

	while (i < length) {
		char c = text[i];

		if (size_t continuation = Continuation(text, i)) {
			i += continuation;
			line++;
			continue;
		}
		if (c == '\n') {
			line++;
			i++;
			atLineStart = true;
			continue;
		}
		if (IsBlank(c)) {
			i++;
			continue;
		}

		// comments (they do not end the "start of the line")
		if (c == '/' && i + 1 < length && text[i + 1] == '/') {
			while (i < length && text[i] != '\n') {
				if (size_t continuation = Continuation(text, i)) {
					i += continuation;
					line++;
				} else {
					i++;
				}
			}
			continue;
		}
		if (c == '/' && i + 1 < length && text[i + 1] == '*') {
			i += 2;
			while (i < length && !(text[i] == '*' && i + 1 < length && text[i + 1] == '/')) {
				if (text[i] == '\n')
					line++;
				i++;
			}
			i = std::min(length, i + 2);
			continue;
		}

		// a directive
		if (c == '#' && atLineStart) {
			atLineStart = false;
			const unsigned directiveLine = line;
			i = SkipBlanks(text, i + 1, &line);
			size_t wordStart = i;
			while (i < length && IsIdentifier(text[i]))
				i++;
			const std::string word = text.substr(wordStart, i - wordStart);

			if (word == "include" || word == "include_next" || word == "import") {
				i = SkipBlanks(text, i, &line);
				if (i < length && (text[i] == '<' || text[i] == '"')) {
					const char close = text[i] == '<' ? '>' : '"';
					const bool global = text[i] == '<';
					size_t end = i + 1;
					while (end < length && text[end] != close && text[end] != '\n')
						end++;
					if (end < length && text[end] == close) {
						std::string name = text.substr(i + 1, end - i - 1);
						if (!name.empty() && conditions.Active())
							dependencies->push_back({name, std::string(), global, directiveLine});
						i = end + 1;
					}
				}
			} else if (word == "if") {
				i = SkipBlanks(text, i, &line);
				size_t tokenStart = i;
				while (i < length && IsIdentifier(text[i]))
					i++;
				const std::string token = text.substr(tokenStart, i - tokenStart);
				// the condition is only a constant if nothing else follows on the line
				size_t rest = SkipBlanks(text, i, &line);
				bool alone = rest >= length || text[rest] == '\n' || (text[rest] == '/' && rest + 1 < length
					&& (text[rest + 1] == '/' || text[rest + 1] == '*'));
				conditions.If(alone && (token == "0" || token == "false"), alone && (token == "1" || token == "true"));
			} else if (word == "ifdef" || word == "ifndef") {
				conditions.If(false, false);
			} else if (word == "else" || word == "elif" || word == "elifdef" || word == "elifndef") {
				conditions.Else();
			} else if (word == "endif") {
				conditions.End();
			}
			continue;
		}
		atLineStart = false;

		// strings and characters: what is in them is no directive and no comment
		if (c == '"') {
			// a raw string: R"delimiter( ... )delimiter", also with the prefixes u8, u, U and L
			if (i > 0 && text[i - 1] == 'R' && (i < 2 || !IsIdentifier(text[i - 2]) || text[i - 2] == '8' || text[i - 2] == 'u'
					|| text[i - 2] == 'U' || text[i - 2] == 'L')) {
				size_t open = text.find('(', i + 1);
				if (open != std::string::npos && open - i - 1 <= 16) {
					const std::string closing = ")" + text.substr(i + 1, open - i - 1) + "\"";
					size_t end = text.find(closing, open + 1);
					size_t stop = end == std::string::npos ? length : end + closing.size();
					for (size_t k = i; k < stop; k++) {
						if (text[k] == '\n')
							line++;
					}
					i = stop;
					continue;
				}
			}
			i++;
			while (i < length && text[i] != '"' && text[i] != '\n') {
				if (text[i] == '\\' && i + 1 < length && text[i + 1] != '\n')
					i++;
				i++;
			}
			if (i < length && text[i] == '"')
				i++;
			continue;
		}
		if (c == '\'') {
			// a digit separator (1'000) is no character literal
			if (i > 0 && isxdigit((unsigned char) text[i - 1]) && i + 1 < length && isxdigit((unsigned char) text[i + 1])) {
				i++;
				continue;
			}
			i++;
			while (i < length && text[i] != '\'' && text[i] != '\n') {
				if (text[i] == '\\' && i + 1 < length && text[i + 1] != '\n')
					i++;
				i++;
			}
			if (i < length && text[i] == '\'')
				i++;
			continue;
		}
		i++;
	}
}


const std::vector<std::string>&
CFamilyScanner::HeaderFolders() const
{
	if (fHeaderFoldersKnown)
		return fHeaderFolders;
	fHeaderFoldersKnown = true;

	// the places for headers that the system names, in the order of the compiler: user before system, not packaged before packaged
	std::vector<std::string> roots;
	const directory_which places[] = {B_USER_NONPACKAGED_HEADERS_DIRECTORY, B_USER_HEADERS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_HEADERS_DIRECTORY, B_SYSTEM_HEADERS_DIRECTORY};
	for (directory_which place : places) {
		BPath path;
		if (find_directory(place, &path) == B_OK)
			roots.push_back(path.Path());
	}
	// what the path finder knows in addition (other architectures, other roots)
	BStringList found;
	BPathFinder::FindPaths(B_FIND_PATH_HEADERS_DIRECTORY, NULL, B_FIND_PATH_EXISTING_ONLY, found);
	for (int32 index = 0; index < found.CountStrings(); index++) {
		if (std::find(roots.begin(), roots.end(), found.StringAt(index).String()) == roots.end())
			roots.push_back(found.StringAt(index).String());
	}

	auto isFolder = [](const std::string& path) {
		struct stat st;
		return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
	};
	auto subFolders = [&](const std::string& path) {
		std::vector<std::string> names;
		if (DIR* dir = opendir(path.c_str())) {
			while (struct dirent* entry = readdir(dir)) {
				if (entry->d_name[0] == '.')
					continue;
				if (isFolder(path + "/" + entry->d_name))
					names.push_back(entry->d_name);
			}
			closedir(dir);
		}
		std::sort(names.begin(), names.end());
		return names;
	};
	auto add = [&](const std::string& path) {
		if (isFolder(path) && std::find(fHeaderFolders.begin(), fHeaderFolders.end(), path) == fHeaderFolders.end())
			fHeaderFolders.push_back(path);
	};

	for (const std::string& root : roots) {
		// the standard library, then the BeAPI (every kit has a folder, and the add-ons below them), then POSIX and the like
		add(root + "/c++");
		add(root + "/c++/backward");
		add(root + "/os");
		for (const std::string& kit : subFolders(root + "/os")) {
			if (kit != "add-ons" && kit != "be_apps" && kit != "arch" && kit != "package" && kit != "bluetooth")
				add(root + "/os/" + kit);
		}
		for (const char* group : {"add-ons", "be_apps"}) {
			for (const std::string& name : subFolders(root + "/os/" + group))
				add(root + "/os/" + group + "/" + name);
		}
		for (const char* name : {"bsd", "glibc", "gnu", "posix"})
			add(root + "/" + name);
		add(root);
	}
	return fHeaderFolders;
}


void
CFamilyScanner::Resolve(Dependency* dependency, const std::string& sourcePath) const
{
	std::vector<std::string> folders;
	if (!dependency->global) {
		size_t slash = sourcePath.rfind('/');
		folders.push_back(slash == std::string::npos ? "." : sourcePath.substr(0, slash));
	}
	for (const std::string& folder : HeaderFolders())
		folders.push_back(folder);
	dependency->resolved = FindFileInFolders(folders, dependency->name);
}
