/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */

#include "IncludeScanner.h"

#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>

#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sen/Sen.h>
#include <sen/Sensei.h>

#include "LanguageScanner.h"


IncludeScanner::IncludeScanner(const char* filePath, bool self)
	:
	fSourcePath(filePath),
	fSelf(self)
{
}


namespace {

/** the Haiku type of every source file: the language is told by the semantic type */
const char* const kSourceCodeType = "text/x-source-code";

void
SetSemanticType(const char* path, const std::string& type)
{
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return;

	// the Haiku type first: a file with META:TYPE and without BEOS:TYPE is taken for an entry of the MIME database
	BNodeInfo info(&node);
	char haikuType[B_MIME_TYPE_LENGTH];
	if (info.GetType(haikuType) != B_OK || haikuType[0] == '\0')
		info.SetType(kSourceCodeType);

	BString current;
	if (node.ReadAttrString(sen::attr::kType, &current) == B_OK && current == type.c_str())
		return;

	ssize_t written = node.WriteAttr(sen::attr::kType, B_MIME_STRING_TYPE, 0, type.c_str(), type.size() + 1);
	if (written < 0)
		spdlog::warn("could not set {} of {}: {}", sen::attr::kType, path, strerror(written));
	else
		spdlog::info("{} of {} is {}", sen::attr::kType, path, type);
}

}	// namespace


int
IncludeScanner::_Identify(BMessage* reply, std::string* text, Identification* identification)
{
	{
		std::ifstream file(fSourcePath);
		if (!file)
			return 2;
		std::stringstream buffer;
		buffer << file.rdbuf();
		*text = buffer.str();
	}

	*identification = IdentifyFile(fSourcePath, *text);
	if (identification->scanner == NULL) {
		spdlog::error("could not identify the language of {}", fSourcePath);
		return 1;
	}

	SetSemanticType(fSourcePath, identification->semanticType);
	reply->AddString(sensei::key::kSemanticType, identification->semanticType.c_str());
	return 0;
}


int
IncludeScanner::identify(BMessage* reply)
{
	std::string text;
	Identification identification;
	return _Identify(reply, &text, &identification);
}


int
IncludeScanner::run(BMessage* reply)
{
	std::string text;
	Identification identification;
	int status = _Identify(reply, &text, &identification);
	if (status != 0)
		return status;
	const LanguageScanner* scanner = identification.scanner;

	std::vector<Dependency> dependencies;
	scanner->Scan(text, &dependencies);
	for (Dependency& dependency : dependencies)
		scanner->Resolve(&dependency, fSourcePath);

	spdlog::info("found {} references in {} ({})", dependencies.size(), fSourcePath, scanner->Name());

	// one item with an entry per reference in each field (a message keeps the order of the entries of one field)
	BMessage item;
	for (const Dependency& dependency : dependencies) {
		item.AddString(sensei::key::kLabel, dependency.name.c_str());
		// mapped to sen::attr::kToPath so SEN can resolve the target transparently
		item.AddString("path", dependency.resolved.empty() ? dependency.name.c_str() : dependency.resolved.c_str());
		item.AddBool("global", dependency.global);
		if (fSelf)
			item.AddInt32("line", dependency.line);
	}
	reply->AddMessage(sensei::key::kItem, &item);

	return 0;
}
