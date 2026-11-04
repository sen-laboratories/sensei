/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// The settings of BERT and the settings text (common/SettingsText.h): plain C++, no Haiku needed. Build and run with tests/run.sh.

#include <cstdio>
#include <string>

#include "../BertSettings.h"
#include "../../../common/SettingsText.h"

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

namespace st = sensei::settingstext;

int
main()
{
	bert::Settings settings;
	CHECK(settings.choose && settings.authors && settings.maxAuthors == 10);		// (choose and maxAuthors are not in the file: the settings of SEN that all plugins share)

	bert::ParseSettings("# bert\nauthors = no   # no people\n", &settings);
	CHECK(!settings.authors);

	// the settings that all plugins share are not read from the file (a file of an older version may still have them)
	bert::ParseSettings("choose = no\nmax_authors = 12\nauthors = yes\n", &settings);
	CHECK(settings.choose && settings.maxAuthors == 10 && settings.authors);

	// bad values and unknown keys leave what is there
	bert::ParseSettings("authors = maybe\nnonsense = 1\n", &settings);
	CHECK(settings.authors);
	bert::ParseSettings("Authors = no\n", &settings);			// (keys in any case)
	CHECK(!settings.authors);

	std::string reason;
	CHECK(bert::ValidateSetting("authors", "yes") && bert::ValidateSetting("authors", "OFF") && !bert::ValidateSetting("authors", "maybe", &reason) && reason == "yes or no");
	CHECK(!bert::ValidateSetting("choose", "yes", &reason) && reason == "no such setting");			// (shared by all plugins now)
	CHECK(!bert::ValidateSetting("max_authors", "5", &reason) && reason == "no such setting");
	CHECK(!bert::ValidateSetting("overwrite", "yes", &reason) && reason == "no such setting");		// (the action decides, no setting)
	CHECK(!bert::ValidateSetting("authors", "yes # no") && !bert::ValidateSetting("colour", "red", &reason) && reason == "no such setting");

	// what is written is what is read, and the rest of the file stays
	const std::string text = "# bert\nauthors = yes   # people\nunknown = kept\n";
	const std::string changed = st::Update(text, {{"authors", "no"}});
	CHECK(changed == "# bert\nauthors = no # people\nunknown = kept\n");
	bert::Settings back;
	bert::ParseSettings(changed, &back);
	CHECK(!back.authors);
	CHECK(st::Update("", {{"authors", "no"}}) == "authors = no\n");
	CHECK(st::Update("# authors = x\n", {{"authors", "no"}}) == "# authors = x\nauthors = no\n");		// (a comment is not the key)
	CHECK(st::Update("authors = yes", {{"authors", "no"}}) == "authors = no\n");

	std::printf("%d checks, %d failed\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
