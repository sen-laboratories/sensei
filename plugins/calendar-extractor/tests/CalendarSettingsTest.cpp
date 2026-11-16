/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
// Plain C++ test of the settings of the calendar extractor.
#include "../core/CalendarSettings.h"

#include <stdio.h>

using namespace calendar;

static int sChecks = 0, sFailures = 0;
#define CHECK(x) do { sChecks++; if (!(x)) { sFailures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int main()
{
	Settings s;
	CHECK(s.rename && s.description && s.myself.empty());
	ParseSettings("# mine\nrename = no\ndescription = YES\nmyself = Gregor B. Rosenauer <gregor@senauer.ro>\nunknown = 1\nrename = maybe\n", &s);
	CHECK(!s.rename && s.description && s.myself == "Gregor B. Rosenauer <gregor@senauer.ro>");		// ("maybe" is no value: what was there stays)

	CHECK(s.renamePattern == "{date}_{time}_{title}");
	ParseSettings("rename_pattern = {year}-{title}\n", &s);
	CHECK(s.renamePattern == "{year}-{title}");
	ParseSettings("rename_pattern = {bad}\n", &s);
	CHECK(s.renamePattern == "{year}-{title}");		// (a bad pattern leaves what was there)
	CHECK(ValidateSetting("rename_pattern", "{date} {title}") && !ValidateSetting("rename_pattern", "{x}") && !ValidateSetting("rename_pattern", ""));

	std::string reason;
	CHECK(ValidateSetting("rename", "yes") && ValidateSetting("description", "no") && !ValidateSetting("rename", "maybe", &reason) && reason == "yes or no");
	CHECK(ValidateSetting("myself", "") && ValidateSetting("myself", "Jane Roe <jane@example.org>") && !ValidateSetting("myself", "a # b") && !ValidateSetting("nothing", "x"));

	std::string name, email;
	SplitPerson("Gregor B. Rosenauer <gregor@senauer.ro>", &name, &email);
	CHECK(name == "Gregor B. Rosenauer" && email == "gregor@senauer.ro");
	SplitPerson("gregor@senauer.ro", &name, &email);
	CHECK(name.empty() && email == "gregor@senauer.ro");
	SplitPerson("Gregor", &name, &email);
	CHECK(name == "Gregor" && email.empty());

	// who the user is: by the address if the setting has one, else by the name
	CHECK(IsMyself(s, "Someone Else", "GREGOR@senauer.ro") && !IsMyself(s, "Gregor B. Rosenauer", "other@example.org") && !IsMyself(s, "Gregor B. Rosenauer", ""));
	Settings byName;
	byName.myself = "Jane Roe";
	CHECK(IsMyself(byName, "Roe, Jane", "any@example.org") && IsMyself(byName, "jane roe", "") && !IsMyself(byName, "Jane Doe", ""));
	Settings byMail;
	byMail.myself = "jane@example.org";
	CHECK(IsMyself(byMail, "", "Jane@Example.org") && !IsMyself(byMail, "Jane Roe", ""));
	CHECK(!IsMyself(Settings(), "Jane", "jane@example.org"));

	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
