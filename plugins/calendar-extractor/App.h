/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Application.h>

#include "../../common/SettingsSession.h"
#include "CalendarService.h"
#include "CalendarSettingsForm.h"

class App : public BApplication {
public:
							App();
	virtual void			AboutRequested();
	virtual void			MessageReceived(BMessage* message);
	/** a settings session that is over, or that the window has left, ends the application */
	virtual void			Pulse();
	/** the Enrich of the file manager (Akita; B_REFS_RECEIVED) and a double click on a file of one of the relations */
	virtual void			RefsReceived(BMessage* message);
	virtual void			ArgvReceived(int32 argc, char** argv);
	int32					ExitCode() const { return fExitCode; }

private:
	/** identify, enrich or resolve, as the message says; the answer goes to the one who asked */
	status_t				_Handle(BMessage* message);
	void					_PrintUsage(const char* error = NULL) const;

	CalendarService::Options	fOptions;
	calendar::SettingsProvider	fSettingsProvider;
	sensei::SettingsSession	fSession;
	entry_ref				fLibrary;			///< for a run from the command line: where the people and places are made (--library), not the Library of SEN
	bool					fHasLibrary = false;
	int32					fExitCode;
	bool					fNotify;
};
