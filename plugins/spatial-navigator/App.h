/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Application.h>

#include "SpatialSettingsForm.h"

/**
 * The spatial navigator, the plugin that works out the spatial relations between places: within and contains, covered by and covers, touches, overlaps, equal to. They are not stored: the
 * server asks (sensei::cmd::kResolve, see Sensei.h) when the user opens one of them at a place, and the answer is what is right then. See SpatialResolver.h.
 */
class App : public BApplication
{
public:
					App();
	virtual void	AboutRequested();
	virtual void	MessageReceived(BMessage* message);
	/** ends the instance that the settings window started when the window is gone (see SettingsSession) */
	virtual void	Pulse();
	/** the file of a spatial relation (double-clicked in a relation folder): the place that it stands for is opened with the application of its type (Shoji) */
	virtual void	RefsReceived(BMessage* message);
	virtual void	ArgvReceived(int32 argc, char** argv);
	int32			ExitCode() const { return fExitCode; }

private:
	void			_Resolve(BMessage* message);

	int32			fExitCode;
	bool			fDebug;
	/** the settings window of SEN asks for the settings and sends the changes (see SpatialSettingsForm.h) */
	spatial::SettingsProvider	fSettingsProvider;
	sensei::SettingsSession		fSession;
};
