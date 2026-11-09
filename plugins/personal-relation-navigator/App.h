/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Entry.h>
#include <Message.h>

#include <string>
#include <vector>

#include "../../common/TargetOpener.h"

/**
 * @brief Navigator for Personal Relations (knows, friend, colleague, mentor, employment...) and for the relation *status* of a person.
 *
 * For the static personal relations it opens the target person or organization in Shoji (TargetOpener.h).
 *
 * The *status* relation is dynamic (the server asks with sensei::cmd::kResolve when the user opens it): its targets are the recent posts of the person, as Markdown files. They are the posts
 * that Robin (the person enricher) wrote, and are fetched again here when they are more than an hour old, from the feed that the person has (an RSS or Atom feed, the outbox of an
 * ActivityPub account, Bluesky), so that what is shown is current and a machine that is offline shows what it had. A person without a feed or links is enriched first if the setting
 * *Enrich automatically on demand* says so, else a notification tells that there is nothing yet.
 */
class App : public sensei::TargetOpenerApp
{
public:
	App();

	virtual void MessageReceived(BMessage* message) override;
	virtual void ArgvReceived(int32 argc, char** argv) override;

	int32 ExitCode() const { return fExitCode; }

private:
	/** a post that the relation shows */
	struct Status {
		entry_ref	ref;
		std::string	title;
	};

	void _Resolve(BMessage* message);
	void _ResolveStatus(const entry_ref& file, std::vector<Status>* statuses);

	bool	fDebug;
	int32	fExitCode;
};
