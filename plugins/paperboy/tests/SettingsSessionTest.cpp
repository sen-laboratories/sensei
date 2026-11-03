/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// The settings protocol of paperboy (common/SettingsSession.h, PaperSettingsForm.cpp), as the settings window of SEN uses it: the plugin is started with the request, answers
// to the messenger in it, takes changes into its settings file and ends with the session. Haiku only; needs the built SenPaperboy (bin/) and changes the settings file of
// paperboy for a moment (it is put back):
//   g++ -std=c++20 -Wall -Wno-multichar -I/boot/home/config/non-packaged/develop/headers tests/SettingsSessionTest.cpp -o /tmp/settings-session-test -lbe && /tmp/settings-session-test bin/SenPaperboy

#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Handler.h>
#include <Looper.h>
#include <Path.h>
#include <Roster.h>

#include <stdio.h>
#include <string.h>
#include <string>

#include <sen/Sen.h>
#include <sen/Sensei.h>
#include <sen/SenSettingsForm.h>

namespace form = sen::settings::form;

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

/** the window: it gets the answers that are sent to it */
class Receiver : public BLooper {
public:
	Receiver() : BLooper("receiver"), got(0) {}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == sensei::cmd::kSettingsReply) {
			reply = *message;
			session = message->ReturnAddress();
			got++;
		} else
			BLooper::MessageReceived(message);
	}

	BMessage	reply;
	BMessenger	session;
	int32		got;
};

static bool
WaitFor(Receiver* receiver, int32 count)
{
	for (int i = 0; i < 150 && receiver->got < count; i++)
		snooze(100000);
	return receiver->got >= count;
}

static bool
Gone(const BMessenger& messenger)
{
	for (int i = 0; i < 160; i++) {
		if (!messenger.IsValid())
			return true;
		snooze(100000);
	}
	return false;
}

static std::string
ReadFile(const BPath& path)
{
	BFile file(path.Path(), B_READ_ONLY);
	off_t size = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK)
		return "<none>";
	std::string text(size, '\0');
	file.Read(&text[0], size);
	return text;
}

static void
WriteFile(const BPath& path, const std::string& text)
{
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	file.Write(text.data(), text.size());
}

int main(int argc, char** argv)
{
	BApplication app("application/x-vnd.sen-labs.paperboy-settings-test");
	if (argc < 2) {
		printf("usage: settings-session-test <SenPaperboy>\n");
		return 1;
	}
	entry_ref plugin;
	CHECK(get_ref_for_path(argv[1], &plugin) == B_OK);

	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	path.Append("sen");
	create_directory(path.Path(), 0755);
	path.Append("paperboy.settings");
	const std::string original = ReadFile(path);
	WriteFile(path, "# my settings\nemail = old@example.org   # for the polite pool\nmax_authors = 4\nunknown = kept\n");

	Receiver* receiver = new Receiver();
	receiver->Run();

	// the plugin is started with the request and answers to the messenger in it
	BMessage ask(sensei::cmd::kSettings);
	ask.AddMessenger(sensei::key::kReplyTo, BMessenger(receiver));
	ask.AddInt32("serial", 7);
	CHECK(be_roster->Launch(&plugin, &ask) == B_OK);
	CHECK(WaitFor(receiver, 1));
	CHECK(receiver->session.IsValid());		// (the instance that has the session)

	BMessage settings, schema, defaults;
	CHECK(receiver->reply.GetInt32(form::kFieldResult, -1) == B_OK);
	CHECK(receiver->reply.GetInt32("serial", 0) == 7);
	CHECK(strcmp(receiver->reply.GetString(sensei::key::kPlugin, ""), "application/x-vnd.sen-plugin.sen-labs.paperboy") == 0);
	CHECK(receiver->reply.FindMessage(form::kFieldSettings, &settings) == B_OK);
	CHECK(receiver->reply.FindMessage(form::kFieldSchema, &schema) == B_OK);
	CHECK(receiver->reply.FindMessage(form::kFieldDefaults, &defaults) == B_OK);
	CHECK(strcmp(settings.GetString("email", ""), "old@example.org") == 0);		// the file, as it is
	int32 number = 0;
	CHECK(settings.FindInt32("max_authors", &number) == B_OK && number == 4);
	CHECK(settings.GetBool("rename", false));									// (the default: not in the file)
	CHECK(strcmp(settings.GetString("authors", ""), "all") == 0);
	CHECK(strcmp(defaults.GetString("rename_pattern", ""), "{title}") == 0);
	BMessage authors;
	CHECK(schema.FindMessage("authors", &authors) == B_OK && authors.CountNames(B_ANY_TYPE) >= 4);
	CHECK(strcmp(authors.GetString(form::kAttrKind, ""), form::kKindChoice) == 0);
	BMessage key;
	CHECK(schema.FindMessage("api_key", &key) == B_OK && strcmp(key.GetString(form::kAttrKind, ""), form::kKindSecret) == 0);

	// a change: stored in the file, the comment and the other lines stay, the answer has the settings as they are
	BMessage change;
	change.AddString("email", "new@example.org");
	change.AddInt32("max_authors", 7);
	change.AddBool("rename", false);
	BMessage set(sensei::cmd::kSettingsSet), answer;
	set.AddMessage(form::kFieldSettings, &change);
	CHECK(receiver->session.SendMessage(&set, &answer, 5000000, 5000000) == B_OK);
	CHECK(answer.GetInt32(form::kFieldResult, -1) == B_OK);
	BMessage now;
	CHECK(answer.FindMessage(form::kFieldSettings, &now) == B_OK);
	CHECK(strcmp(now.GetString("email", ""), "new@example.org") == 0 && now.GetInt32("max_authors", 0) == 7 && !now.GetBool("rename", true));
	std::string file = ReadFile(path);
	CHECK(file.find("# my settings\n") == 0);
	CHECK(file.find("email = new@example.org # for the polite pool\n") != std::string::npos);
	CHECK(file.find("max_authors = 7\n") != std::string::npos);
	CHECK(file.find("rename = no\n") != std::string::npos);
	CHECK(file.find("unknown = kept\n") != std::string::npos);

	// values that are not valid are refused and told; the valid ones of the same change are kept
	BMessage bad;
	bad.AddInt32("max_authors", 500);
	bad.AddString("nonsense", "x");
	bad.AddString("authors", "orcid");
	bad.AddString("email", "not an address");
	BMessage setBad(sensei::cmd::kSettingsSet);
	setBad.AddMessage(form::kFieldSettings, &bad);
	answer.MakeEmpty();
	CHECK(receiver->session.SendMessage(&setBad, &answer, 5000000, 5000000) == B_OK);
	CHECK(answer.GetInt32(form::kFieldResult, B_OK) != B_OK);
	const std::string detail = answer.GetString(sen::key::kDetail, "");
	CHECK(detail.find("max_authors") != std::string::npos && detail.find("nonsense") != std::string::npos && detail.find("email") != std::string::npos);
	CHECK(answer.FindMessage(form::kFieldSettings, &now) == B_OK);
	CHECK(now.GetInt32("max_authors", 0) == 7 && strcmp(now.GetString("authors", ""), "orcid") == 0 && strcmp(now.GetString("email", ""), "new@example.org") == 0);
	file = ReadFile(path);
	CHECK(file.find("authors = orcid\n") != std::string::npos && file.find("nonsense") == std::string::npos);

	// the window is done: the plugin ends
	BMessenger session = receiver->session;
	session.SendMessage(sensei::cmd::kSettingsClose);
	CHECK(Gone(session));

	// the window goes away without a word: the plugin ends by itself
	receiver->got = 0;
	BMessage again(sensei::cmd::kSettings);
	again.AddMessenger(sensei::key::kReplyTo, BMessenger(receiver));
	CHECK(be_roster->Launch(&plugin, &again) == B_OK);
	CHECK(WaitFor(receiver, 1));
	session = receiver->session;
	CHECK(session.IsValid());
	receiver->Lock();
	receiver->Quit();
	CHECK(Gone(session));

	WriteFile(path, original == "<none>" ? "" : original);
	if (original == "<none>")
		BEntry(path.Path()).Remove();
	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
