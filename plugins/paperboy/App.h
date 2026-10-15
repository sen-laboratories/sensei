/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Application.h>

#include "PaperSettings.h"

class Paperboy;

class App : public BApplication
{
public:
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
	Paperboy*					_Service();
	void						_PrintUsage(const char* error = NULL) const;

	paperboy::Settings			fSettings;
	Paperboy*					fPaperboy;
	bool						fDebug;
	int							fExitCode;
};
