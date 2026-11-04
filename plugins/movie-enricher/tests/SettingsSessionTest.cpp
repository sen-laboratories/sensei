/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// The settings protocol of the movie enricher (common/SettingsSession.h, BertSettingsForm.cpp), as the settings window of SEN uses it: the plugin is started with the request, answers
// to the messenger in it, takes changes into its settings file and ends with the session. Haiku only; needs the built Senthe movie enricher (bin/) and changes the settings file of
// the movie enricher for a moment (it is put back):
//   g++ -std=c++20 -Wall -Wno-multichar -I/boot/home/config/non-packaged/develop/headers tests/SettingsSessionTest.cpp -o /tmp/settings-session-test -lbe && /tmp/settings-session-test bin/<the program>

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
	BApplication app("application/x-vnd.sen-labs.movies-settings-test");
	if (argc < 2) {
		printf("usage: settings-session-test <the program of the movie enricher>\n");
		return 1;
	}
	entry_ref plugin;
	CHECK(get_ref_for_path(argv[1], &plugin) == B_OK);

	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	path.Append("sen");
	create_directory(path.Path(), 0755);
	path.Append("movies.settings");
	const std::string original = ReadFile(path);
	WriteFile(path, "# my settings\napi_key = abc123   # mine\nchoose = no\nunknown = kept\n");

	Receiver* receiver = new Receiver();
	receiver->Run();

	BMessage ask(sensei::cmd::kSettings);
	ask.AddMessenger(sensei::key::kReplyTo, BMessenger(receiver));
	ask.AddInt32("serial", 5);
	CHECK(be_roster->Launch(&plugin, &ask) == B_OK);
	CHECK(WaitFor(receiver, 1));
	CHECK(receiver->session.IsValid());

	BMessage settings, schema, defaults;
	CHECK(receiver->reply.GetInt32(form::kFieldResult, -1) == B_OK);
	CHECK(strcmp(receiver->reply.GetString(sensei::key::kPlugin, ""), "application/x-vnd.sen-plugin.sen-labs.movie-enricher") == 0);
	CHECK(receiver->reply.FindMessage(form::kFieldSettings, &settings) == B_OK);
	CHECK(receiver->reply.FindMessage(form::kFieldSchema, &schema) == B_OK);
	CHECK(receiver->reply.FindMessage(form::kFieldDefaults, &defaults) == B_OK);
	CHECK(strcmp(settings.GetString("api_key", ""), "abc123") == 0 && !settings.GetBool("choose", true));
	CHECK(strcmp(settings.GetString("language", ""), "en-US") == 0 && settings.GetBool("cast", false) && settings.GetInt32("max_cast", 0) == 10);
	BMessage key, language;
	CHECK(schema.FindMessage("api_key", &key) == B_OK && strcmp(key.GetString(form::kAttrKind, ""), form::kKindSecret) == 0);
	CHECK(schema.FindMessage("language", &language) == B_OK && language.CountNames(B_STRING_TYPE) > 3);

	BMessage change;
	change.AddString("language", "de-DE");
	change.AddInt32("max_cast", 5);
	BMessage set(sensei::cmd::kSettingsSet), answer;
	set.AddMessage(form::kFieldSettings, &change);
	CHECK(receiver->session.SendMessage(&set, &answer, 5000000, 5000000) == B_OK);
	CHECK(answer.GetInt32(form::kFieldResult, -1) == B_OK);
	std::string file = ReadFile(path);
	CHECK(file.find("api_key = abc123   # mine\n") != std::string::npos && file.find("unknown = kept\n") != std::string::npos);
	CHECK(file.find("language = de-DE\n") != std::string::npos && file.find("max_cast = 5\n") != std::string::npos);

	BMessage bad;
	bad.AddString("api_key", "has space");
	bad.AddString("language", "German");
	bad.AddBool("crew", false);
	BMessage setBad(sensei::cmd::kSettingsSet);
	setBad.AddMessage(form::kFieldSettings, &bad);
	answer.MakeEmpty();
	CHECK(receiver->session.SendMessage(&setBad, &answer, 5000000, 5000000) == B_OK);
	CHECK(answer.GetInt32(form::kFieldResult, B_OK) != B_OK);
	const std::string detail = answer.GetString(sen::key::kDetail, "");
	CHECK(detail.find("api_key") != std::string::npos && detail.find("language") != std::string::npos);
	BMessage now;
	CHECK(answer.FindMessage(form::kFieldSettings, &now) == B_OK && strcmp(now.GetString("api_key", ""), "abc123") == 0 && !now.GetBool("crew", true));

	BMessenger session = receiver->session;
	session.SendMessage(sensei::cmd::kSettingsClose);
	CHECK(Gone(session));

	WriteFile(path, original == "<none>" ? "" : original);
	if (original == "<none>")
		BEntry(path.Path()).Remove();
	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
