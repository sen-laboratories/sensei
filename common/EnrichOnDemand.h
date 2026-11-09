/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file EnrichOnDemand.h
 * @brief A navigator that needs a file to be enriched first (a place that has no position, a person that has no feed) lets the enrichment plugin do it now (header only).
 *
 * The user decides whether that is done at all: the setting *Enrich automatically on demand* in the settings of SEN (Plugins > Enrich, read with sen::plugins::EnrichOnDemand()). A navigator
 * that is asked by the server cannot ask the server to enrich (the server waits for its answer), so it starts the plugin as the server would, with the file, and waits for it to end:
 *
 *     if (sen::plugins::EnrichOnDemand() && sensei::EnrichNow("application/x-vnd.sen-plugin.sen-labs.place-enricher", file))
 *         ... // read the file again
 */

#include <Entry.h>
#include <Message.h>
#include <OS.h>
#include <Roster.h>

#include <stdio.h>
#include <string.h>

namespace sensei {

/**
 * Starts the enrichment plugin of the signature with the file (it asks nothing of the user: `auto`; it keeps what the file has: no `wipe`) and waits until it has ended, or until @p timeout.
 * @param flag a field of the plugin's message that is set to true (a plugin that has more than one thing to do: Robin fetches only the posts for `posts`)
 * @return true if the plugin was started and has ended in time (not that it found anything: read the file again)
 */
inline bool
EnrichNow(const char* pluginSignature, const entry_ref& file, bigtime_t timeout = 40 * 1000000LL, const char* flag = NULL)
{
	entry_ref plugin;
	if (be_roster->FindApp(pluginSignature, &plugin) != B_OK)
		return false;

	BMessage launch(B_REFS_RECEIVED);
	launch.AddRef("refs", &file);
	launch.AddBool("wipe", false);
	launch.AddBool("auto", true);
	if (flag != NULL)
		launch.AddBool(flag, true);
	team_id team = -1;
	status_t result = be_roster->Launch(&plugin, &launch, &team);
	if (result != B_OK && result != B_ALREADY_RUNNING) {
		printf("could not start %s: %s\n", plugin.name, strerror(result));
		return false;
	}
	const bigtime_t deadline = system_time() + timeout;
	app_info info;
	while (team >= 0 && be_roster->GetRunningAppInfo(team, &info) == B_OK) {
		if (system_time() >= deadline)
			return false;
		snooze(200000);
	}
	return true;
}

}	// namespace sensei
