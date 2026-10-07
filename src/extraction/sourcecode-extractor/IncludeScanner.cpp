/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */

#include "IncludeScanner.h"

#include <FindDirectory.h>
#include <Path.h>

#include <clang-c/Index.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sen/Sen.h>

namespace {

struct Include {
	std::string	name;		///< as written in the directive
	std::string	resolved;	///< the file that clang found, empty if it did not
	bool		global;		///< <name> instead of "name"
	unsigned	line;
};

struct Scan {
	std::string			text;		///< the source, to see how the directive is written
	std::vector<Include> includes;
};

std::string
ToString(CXString string)
{
	const char* text = clang_getCString(string);
	std::string result(text != NULL ? text : "");
	clang_disposeString(string);
	return result;
}

/** is the name in the directive in angle brackets? The directive starts at the offset in the text. */
bool
IsAngled(const std::string& text, unsigned offset)
{
	size_t lineEnd = text.find('\n', offset);
	size_t quote = text.find('"', offset);
	size_t angle = text.find('<', offset);
	if (angle == std::string::npos || (lineEnd != std::string::npos && angle > lineEnd))
		return false;
	return quote == std::string::npos || angle < quote;
}

CXChildVisitResult
Visit(CXCursor cursor, CXCursor, CXClientData data)
{
	if (clang_getCursorKind(cursor) != CXCursor_InclusionDirective)
		return CXChildVisit_Continue;

	// the directives of the file itself; the ones in the headers belong to the headers
	CXSourceLocation location = clang_getCursorLocation(cursor);
	if (!clang_Location_isFromMainFile(location))
		return CXChildVisit_Continue;

	Scan* scan = static_cast<Scan*>(data);

	Include include;
	include.name = ToString(clang_getCursorSpelling(cursor));

	CXFile unused;
	unsigned column, offset;
	clang_getSpellingLocation(location, &unused, &include.line, &column, &offset);
	clang_getSpellingLocation(clang_getRangeStart(clang_getCursorExtent(cursor)), &unused, &include.line, &column, &offset);
	include.global = IsAngled(scan->text, offset);

	CXFile included = clang_getIncludedFile(cursor);
	if (included != NULL) {
		BPath path(ToString(clang_getFileName(included)).c_str(), NULL, true);	// normalized
		include.resolved = path.Path() != NULL ? path.Path() : "";
	}

	scan->includes.push_back(include);
	return CXChildVisit_Continue;
}

}	// namespace


IncludeScanner::IncludeScanner(const char* filePath, bool self)
	:
	fSourcePath(filePath),
	fSelf(self)
{
}


int
IncludeScanner::run(BMessage* reply)
{
	Scan scan;
	{
		std::ifstream file(fSourcePath);
		if (!file)
			return 2;
		std::stringstream buffer;
		buffer << file.rdbuf();
		scan.text = buffer.str();
	}

	// where headers are: the same folders a compiler on Haiku searches
	std::vector<std::string> arguments;
	const directory_which folders[] = {B_SYSTEM_HEADERS_DIRECTORY, B_SYSTEM_NONPACKAGED_HEADERS_DIRECTORY,
		B_USER_HEADERS_DIRECTORY, B_USER_NONPACKAGED_HEADERS_DIRECTORY};
	for (directory_which folder : folders) {
		BPath path;
		if (find_directory(folder, &path) == B_OK) {
			arguments.push_back("-I");
			arguments.push_back(path.Path());
		}
	}
	std::string source(fSourcePath);
	bool isC = source.size() > 2 && source.compare(source.size() - 2, 2, ".c") == 0;
	arguments.push_back("-x");
	arguments.push_back(isC ? "c" : "c++");

	std::vector<const char*> argv;
	for (const std::string& argument : arguments)
		argv.push_back(argument.c_str());

	CXIndex index = clang_createIndex(0, 0);
	CXTranslationUnit unit = NULL;
	CXErrorCode error = clang_parseTranslationUnit2(index, fSourcePath, argv.data(), (int) argv.size(), NULL, 0,
		CXTranslationUnit_DetailedPreprocessingRecord | CXTranslationUnit_SkipFunctionBodies
			| CXTranslationUnit_Incomplete,
		&unit);
	if (error != CXError_Success || unit == NULL) {
		spdlog::error("could not scan {} for includes (libclang error {})", fSourcePath, (int) error);
		clang_disposeIndex(index);
		return 1;
	}

	// missing headers are reported as errors but do not matter here: the include is found anyway
	clang_visitChildren(clang_getTranslationUnitCursor(unit), Visit, &scan);
	clang_disposeTranslationUnit(unit);
	clang_disposeIndex(index);

	spdlog::info("found {} includes in {}", scan.includes.size(), fSourcePath);

	// one item with an entry per include in each field (a message keeps the order of the entries of one field)
	BMessage item;
	for (const Include& include : scan.includes) {
		item.AddString(sensei::key::kLabel, include.name.c_str());
		// mapped to sen::attr::kToPath so SEN can resolve the target transparently
		item.AddString("path", include.resolved.empty() ? include.name.c_str() : include.resolved.c_str());
		item.AddBool("global", include.global);
		if (fSelf)
			item.AddInt32("line", include.line);
	}
	reply->AddMessage(sensei::key::kItem, &item);

	return 0;
}
