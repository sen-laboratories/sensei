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
	CHECK(settings.choose && settings.authors && settings.maxAuthors == 100);

	bert::ParseSettings("# bert\nchoose = no   # take the best\nauthors = yes\nmax_authors = 12\n", &settings);
	CHECK(!settings.choose && settings.authors && settings.maxAuthors == 12);

	// bad values and unknown keys leave what is there
	bert::ParseSettings("max_authors = 0\nchoose = maybe\nnonsense = 1\nmax_authors = many\n", &settings);
	CHECK(settings.maxAuthors == 12 && !settings.choose);
	bert::ParseSettings("max_authors = 101\n", &settings);
	CHECK(settings.maxAuthors == 12);
	bert::ParseSettings("MAX_AUTHORS = 100\nAuthors = no\n", &settings);			// (keys in any case)
	CHECK(settings.maxAuthors == 100 && !settings.authors);

	std::string reason;
	CHECK(bert::ValidateSetting("choose", "yes") && bert::ValidateSetting("choose", "OFF") && !bert::ValidateSetting("choose", "maybe", &reason) && reason == "yes or no");
	CHECK(bert::ValidateSetting("max_authors", "1") && bert::ValidateSetting("max_authors", "100"));
	CHECK(!bert::ValidateSetting("max_authors", "0") && !bert::ValidateSetting("max_authors", "101") && !bert::ValidateSetting("max_authors", "") && !bert::ValidateSetting("max_authors", "5x"));
	CHECK(!bert::ValidateSetting("overwrite", "yes", &reason) && reason == "no such setting");		// (the action decides, no setting)
	CHECK(!bert::ValidateSetting("authors", "yes # no") && !bert::ValidateSetting("colour", "red", &reason) && reason == "no such setting");

	// what is written is what is read, and the rest of the file stays
	const std::string text = "# bert\nchoose = yes   # ask\nunknown = kept\n";
	const std::string changed = st::Update(text, {{"choose", "no"}, {"max_authors", "5"}});
	CHECK(changed == "# bert\nchoose = no # ask\nunknown = kept\nmax_authors = 5\n");
	bert::Settings back;
	bert::ParseSettings(changed, &back);
	CHECK(!back.choose && back.maxAuthors == 5);
	CHECK(st::Update("", {{"authors", "no"}}) == "authors = no\n");
	CHECK(st::Update("# choose = x\n", {{"choose", "no"}}) == "# choose = x\nchoose = no\n");		// (a comment is not the key)
	CHECK(st::Update("choose = yes", {{"choose", "no"}}) == "choose = no\n");

	std::printf("%d checks, %d failed\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
