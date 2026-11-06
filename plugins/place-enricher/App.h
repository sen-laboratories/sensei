/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Application.h>
#include <Entry.h>

#include <string>
#include <vector>

#include "../../common/BaseEnricher.h"
#include "PlaceCache.h"
#include "PlaceNominatim.h"
#include "PlaceSettingsForm.h"
#include "PlaceWikidata.h"

class MappingUtil;

/**
 * SUESS (Semantic Urban & Environmental Spaces for SEN), the place enricher: looks a place up at OpenStreetMap Nominatim (by the name of the file or its title, by the link to OpenStreetMap
 * that an earlier run wrote, or, if the user allows it, by the position of the file), writes its attributes (name, other names, kind of place, position, bounding box, outline, address)
 * to the file, and takes a picture of the place from Wikidata and Wikimedia Commons as its thumbnail.
 *
 * Data (c) OpenStreetMap contributors, Open Database License (https://www.openstreetmap.org/copyright). Answers are kept for a week and requests to Nominatim are one second apart (its
 * usage policy). Parents, neighbours and other relations of a place are not made here: the spatial navigator works them out when they are asked for.
 */
class App : public BApplication
{
public:
	virtual void	AboutRequested();
					App();
	virtual			~App();
	virtual void	RefsReceived(BMessage* message);
	virtual void	ArgvReceived(int32 argc, char** argv);
	/** the settings window of SEN asks for the settings and sends the changes (see PlaceSettingsForm.h) */
	virtual void	MessageReceived(BMessage* message);
	/** ends the application when a settings session is over */
	virtual void	Pulse();

	/** 0, or 1 if something went wrong (a cancelled choice is no error) */
	int32			ExitCode() const { return fExitCode; }

private:
	enum Service {
		kNominatim,		///< kept, and one second between two requests
		kWikimedia		///< Wikidata and Commons: kept
	};

	status_t		Enrich(BMessage* message, BMessage* reply);
	/** asks a service (the answer of an address that was asked in the last week is the one that is kept); B_BUSY if the service says that there are too many requests (429), B_NOT_ALLOWED if it refuses (403) */
	status_t		Get(const std::string& url, Service service, std::string* body);
	/** the picture itself: not kept (it is the thumbnail of the file) */
	status_t		GetImage(const std::string& url, std::string* data);
	/** the place that is meant: by the link to OpenStreetMap that the file has, else by the position (if the user allows it and the file has no title), else by a search (and the user's choice if there are several) */
	status_t		FindPlace(const entry_ref& file, place::Hit* hit);
	status_t		ChoosePlace(const std::vector<place::Hit>& hits, const std::string& wanted, place::Hit* chosen);
	void			ShowError(const char* message);
	void			PrintUsage(const char* error = NULL);
	/** the settings as they are now (the file, the settings of SEN that all plugins share): the instance may have been started before the user changed them; what the command line gave is kept */
	void			_ReloadSettings();

	place::Settings			fSettings;
	place::SettingsProvider	fSettingsProvider;
	sensei::SettingsSession	fSession;
	MappingUtil*			fMapper;
	BaseEnricher*			fHttp;
	place::DiskCache*		fCache;
	place::RateLimiter*		fLimiter;
	std::string				fLanguage;			///< what is asked for: the setting, else the language of the system
	std::string				fWanted;
	bool					fDebug;
	bool					fLanguageGiven;
	bool					fPositionGiven;
	bool					fHasPositionHint;	///< the file has a position (to say so when the name finds nothing)
	bool					fOverwrite;
	bool					fAuto;
	int32					fExitCode;
};
