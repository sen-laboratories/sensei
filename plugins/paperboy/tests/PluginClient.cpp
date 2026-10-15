/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// Talks to the plugin the way the SEN server does: launches it, sends the request with a reply target, waits for the answer.
// Build and run on Haiku: tests/plugin-client.sh. Usage: PluginClient <extract|identify|enrich> [--self] <file>

#include <cstdio>
#include <cstring>

#include <Application.h>
#include <Entry.h>
#include <Looper.h>
#include <Messenger.h>
#include <OS.h>
#include <Roster.h>

#include <sen/Sen.h>
#include <sen/Sensei.h>

namespace {

const char* const kSignature = "application/x-vnd.sen-plugin.sen-labs.paperboy";

/** the reply collector of the server: a looper of its own, so that the answer does not wait for the thread that asks */
class Collector : public BLooper {
public:
	Collector() : BLooper("collector"), fSem(create_sem(0, "reply")), fGot(false) {}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == sensei::cmd::kResult) {
			fReply = *message;
			fGot = true;
			release_sem(fSem);
		} else {
			BLooper::MessageReceived(message);
		}
	}

	bool Wait(bigtime_t timeout) { return acquire_sem_etc(fSem, 1, B_RELATIVE_TIMEOUT, timeout) == B_OK && fGot; }
	BMessage fReply;

private:
	sem_id fSem;
	bool fGot;
};

}	// namespace


int
main(int argc, char** argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: PluginClient <extract|identify|enrich> [--self] <file>\n");
		return 2;
	}
	BApplication app("application/x-vnd.sen-labs.test-plugin-client");

	uint32 what = B_REFS_RECEIVED;
	if (std::strcmp(argv[1], "identify") == 0)
		what = sensei::cmd::kIdentify;
	else if (std::strcmp(argv[1], "enrich") == 0)
		what = sensei::cmd::kEnrich;

	bool self = false;
	const char* path = argv[argc - 1];
	for (int i = 2; i < argc - 1; i++)
		self = self || std::strcmp(argv[i], sensei::kOptionSelf) == 0;

	entry_ref ref;
	if (BEntry(path, true).GetRef(&ref) != B_OK) {
		std::fprintf(stderr, "no such file: %s\n", path);
		return 2;
	}

	team_id team;
	status_t status = be_roster->Launch(kSignature, (BMessage*) NULL, &team);
	if (status != B_OK) {
		std::fprintf(stderr, "could not launch the plugin: %s\n", strerror(status));
		return 1;
	}

	Collector* collector = new Collector();
	collector->Run();

	BMessage request(what);
	request.AddRef("refs", &ref);
	request.AddBool(sen::conf::kSelf, self);
	status = BMessenger(kSignature, team).SendMessage(&request, BMessenger(collector));
	if (status != B_OK) {
		std::fprintf(stderr, "could not send the request: %s\n", strerror(status));
		return 1;
	}

	if (!collector->Wait(120 * 1000000LL)) {
		std::fprintf(stderr, "no answer\n");
		return 1;
	}
	collector->Lock();
	collector->fReply.PrintToStream();
	int32 result = collector->fReply.GetInt32(sensei::key::kResult, -1);
	collector->Quit();
	return result == B_OK ? 0 : 1;
}
