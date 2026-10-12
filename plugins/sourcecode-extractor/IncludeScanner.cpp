/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */

#include "IncludeScanner.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sen/Sen.h>

#include "LanguageScanner.h"


IncludeScanner::IncludeScanner(const char* filePath, bool self)
	:
	fSourcePath(filePath),
	fSelf(self)
{
}


int
IncludeScanner::run(BMessage* reply)
{
	std::string text;
	{
		std::ifstream file(fSourcePath);
		if (!file)
			return 2;
		std::stringstream buffer;
		buffer << file.rdbuf();
		text = buffer.str();
	}

	const LanguageScanner* scanner = ScannerForFile(fSourcePath);
	if (scanner == NULL) {
		spdlog::error("no scanner for the language of {}", fSourcePath);
		return 1;
	}

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
