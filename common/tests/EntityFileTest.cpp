/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// FindOrCreateEntityFile (common/EntityFile.h): a new entity goes into the Library of the current context when the SEN server answers, next to the file when it does not; an entity
// that is there is found where it is (in the Library, or next to the file where an earlier version put it) and not made again; another entity with the name is not touched. Haiku
// only. entity-file.sh runs it against the server under test ("server") and without one ("noserver"); it removes what it makes.

#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "../EntityFile.h"

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

static const char* kPerson = "entity/x-vnd.sen-labs.entity.person";

static std::string
PathOf(const entry_ref& ref)
{
	return BPath(&ref).Path();
}

static std::string
ParentOf(const entry_ref& ref)
{
	BPath path(&ref);
	BPath parent;
	path.GetParent(&parent);
	return parent.Path();
}

/** a file of a type, with a key attribute */
static void
MakeFile(const std::string& folder, const char* name, const char* type, const char* key)
{
	BDirectory directory(folder.c_str());
	BFile file;
	directory.CreateFile(name, &file, true);
	if (type != NULL)
		BNodeInfo(&file).SetType(type);
	if (key != NULL)
		file.WriteAttr("test:key", B_STRING_TYPE, 0, key, strlen(key) + 1);
}

int main(int argc, char** argv)
{
	BApplication app("application/x-vnd.sen-labs.entity-file-test");
	const bool server = argc > 1 && strcmp(argv[1], "server") == 0;

	const std::string base = std::string(getenv("TEST_DIR") != NULL ? getenv("TEST_DIR") : "/Develop/test") + "/_entityfile";
	const std::string besideFolder = base + "/beside";
	create_directory(besideFolder.c_str(), 0755);
	MakeFile(besideFolder, "book", "application/pdf", NULL);
	entry_ref book;
	get_ref_for_path((besideFolder + "/book").c_str(), &book);

	auto sameKey = [](const char* key) {
		return [key](BNode& node) {
			BString stored;
			node.ReadAttrString("test:key", &stored);
			return stored.IsEmpty() || stored == key;
		};
	};

	entry_ref made, again;
	bool created = false;
	const std::string unique = "Entity File Test Zz";

	// a new person: in the Library with a server, next to the book without
	CHECK(sensei::FindOrCreateEntityFile(book, kPerson, unique.c_str(), "K1", sameKey("K1"), &made, &created) == B_OK);
	CHECK(created);
	if (server)
		CHECK(PathOf(made).find("/Library/") != std::string::npos && ParentOf(made) != besideFolder);
	else
		CHECK(ParentOf(made) == besideFolder);
	char type[B_MIME_TYPE_LENGTH] = "";
	BNode node(&made);
	BNodeInfo(&node).GetType(type);
	CHECK(strcmp(type, kPerson) == 0);
	node.WriteAttr("test:key", B_STRING_TYPE, 0, "K1", 3);

	// the same person again: found, not made again
	CHECK(sensei::FindOrCreateEntityFile(book, kPerson, unique.c_str(), "K1", sameKey("K1"), &again, &created) == B_OK);
	CHECK(!created && made == again);

	// another person with the name (another key): not touched, the new one is called with the key
	entry_ref other;
	CHECK(sensei::FindOrCreateEntityFile(book, kPerson, unique.c_str(), "K2", sameKey("K2"), &other, &created) == B_OK);
	CHECK(created && !(other == made) && std::string(other.name) == unique + " (K2)");

	// a person that an earlier version made next to the book is found there (and not made again in the Library)
	MakeFile(besideFolder, "Legacy Person Zz", kPerson, "K3");
	CHECK(sensei::FindOrCreateEntityFile(book, kPerson, "Legacy Person Zz", "K3", sameKey("K3"), &again, &created) == B_OK);
	CHECK(!created && ParentOf(again) == besideFolder);

	// a file of the name that is no person is not taken for one (and not replaced)
	MakeFile(ParentOf(made), "Not A Person Zz", "text/plain", NULL);
	entry_ref third;
	CHECK(sensei::FindOrCreateEntityFile(book, kPerson, "Not A Person Zz", "K4", NULL, &third, &created) == B_OK);
	CHECK(created && std::string(third.name) == "Not A Person Zz (K4)");
	CHECK(BEntry(PathOf(third).c_str()).Exists() && BEntry((ParentOf(made) + "/Not A Person Zz").c_str()).Exists());

	// a name with a slash is one name; no name: the key
	entry_ref slash;
	CHECK(sensei::FindOrCreateEntityFile(book, kPerson, "A/B Zz", "K5", NULL, &slash, &created) == B_OK && std::string(slash.name) == "A-B Zz");
	entry_ref nameless;
	CHECK(sensei::FindOrCreateEntityFile(book, kPerson, "  ", "K6Zz", NULL, &nameless, &created) == B_OK && std::string(nameless.name) == "K6Zz");
	CHECK(sensei::FindOrCreateEntityFile(book, kPerson, "", "", NULL, &nameless, &created) == B_BAD_VALUE);

	// remove what was made (not the Library itself)
	for (const entry_ref& ref : {made, other, third, slash, nameless})
		BEntry(&ref).Remove();
	BEntry((ParentOf(made) + "/Not A Person Zz").c_str()).Remove();
	std::string cleanup = "rm -rf '" + base + "'";
	system(cleanup.c_str());

	printf("%s: %d checks, %d failure(s)\n", server ? "with the server" : "without", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
