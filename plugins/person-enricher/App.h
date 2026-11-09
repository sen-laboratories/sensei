/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Application.h>
#include <Entry.h>
#include <Message.h>
#include <String.h>

#include <string>
#include <vector>

#include "../../common/SettingsSession.h"
#include "HttpFetcher.h"
#include "PersonSettingsForm.h"
#include "core/Hunt.h"
#include "core/PersonData.h"

namespace robin {

class App : public BApplication {
public:
	App();
	virtual ~App();

	virtual void AboutRequested() override;
	virtual void MessageReceived(BMessage* message) override;
	virtual void Pulse() override;
	virtual void RefsReceived(BMessage* message) override;
	virtual void ArgvReceived(int32 argc, char** argv) override;

	status_t Enrich(BMessage* message, BMessage* reply);
	status_t EnrichOne(const entry_ref& file, bool wipe, bool automatic, BString* summary);
	/** only the posts of the person, again (the personal relation navigator asks for them): B_OK if there were posts */
	status_t RefreshPostsOf(const entry_ref& file);

	int32 ExitCode() const { return fExitCode; }

private:
	bool _Hunt(const PersonProfile& profile, bool automatic, std::vector<HuntHit>* chosen, bool* cancelled);
	void _BestPhoto(PersonProfile* profile, const HuntHit& hit);

	HttpFetcher fHttp;
	Settings fSettings;					///< from the settings file, read for every file (the settings window may have changed it)
	SettingsProvider fSettingsProvider;
	sensei::SettingsSession fSession;
	bool fDebug;
	int32 fExitCode;
};

} // namespace robin
