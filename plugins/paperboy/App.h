/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Application.h>

#include <string>

#include "PaperSettings.h"

class Paperboy;

class App : public BApplication
{
public:
	virtual void	AboutRequested();
								App();
	virtual						~App();

	virtual void				ArgvReceived(int32 argc, char** argv);
	/** the server asks for the extraction (the default of a plugin) */
	virtual void				RefsReceived(BMessage* message);
	/** the server asks for identify or enrich */
	virtual void				MessageReceived(BMessage* message);

	/** 0 if everything that was asked was done, else 1 (for the command line) */
	int							ExitCode() const { return fExitCode; }

private:
	/** Does what the message asks (its `what`) with its first ref and sends the reply. */
	status_t					_Handle(BMessage* message);
	void						_Notify(const entry_ref& file, status_t result, const std::string& headline, const std::string& text);
	Paperboy*					_Service();
	void						_PrintUsage(const char* error = NULL) const;

	paperboy::Settings			fSettings;
	Paperboy*					fPaperboy;
	bool						fDebug;
	/** the answer to an enrichment is a notification: it was asked for in Tracker, which shows nothing else (the command line prints) */
	bool						fNotify;
	int							fExitCode;
};
