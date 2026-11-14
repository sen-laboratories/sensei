/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Application.h>

class App : public BApplication
{
public:
					App();
	virtual void	AboutRequested();
	virtual void	MessageReceived(BMessage* message);
	/** the file of a temporal relation (double-clicked in a relation folder): the event that it stands for is opened with the application of its type (Shoji) */
	virtual void	RefsReceived(BMessage* message);
	virtual void	ArgvReceived(int32 argc, char** argv);
	int32			ExitCode() const { return fExitCode; }

private:
	void			_Resolve(BMessage* message);

	int32			fExitCode;
	bool			fDebug;
};
