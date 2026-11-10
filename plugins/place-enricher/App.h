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
#include "../../common/HttpCache.h"
#include "PlaceNominatim.h"
#include "PlaceSettingsForm.h"
#include "PlaceWikidata.h"

class MappingUtil;

/**
 * Spaces, the place enricher: looks a place up at OpenStreetMap Nominatim (by the name of the file or its title, by the link to OpenStreetMap
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
	/**
	 * The place that is meant: by the link to OpenStreetMap that the file has; else, if the file has a position and no title and its name is no name of a place (`IMG_0001.jpg`), by the
	 * position alone; else by the name (the title, else the name of the file), where the position, if the file has one, ranks the answers (that is worked out here and sends nothing);
	 * and if a file that has a position and no title has a name that finds no place near the position, by the position. Sending a position to OpenStreetMap is done for a Place or
	 * Location file (the position is the place's own), and for any other file only if the user allows it (the setting, or --position).
	 */
	status_t		FindPlace(const entry_ref& file, place::Hit* hit);
	/** the place of that name (the text of a field of the file: the location of a person), not the place that the file is or says; nothing is asked of the file */
	status_t		FindByQuery(const std::string& query, place::Hit* hit);
	/**
	 * Takes the place that is meant out of the answers of a search: @p near is the position that the file has (NULL: none), which ranks the places by how near they are and, if @p
	 * maxKilometres is more than 0, leaves out those that are farther away. The user chooses if more than one fits (the nearest and best first, with the distance), unless exactly one fits and
	 * is within 5 km of the position.
	 */
	/** the place at a position (reverse geocoding): the position is sent to OpenStreetMap */
	status_t		FindByPosition(const place::Position& position, place::Hit* hit);
	/**
	 * Where the file says that it is (its city, region and country): placed by OpenStreetMap, so that the search for its name stays in that country (a station that is called Westbahnhof
	 * in Wien is not the one in Budapest, which has the name in German, too). False if the file says nothing of where it is, or nothing was found: the search then has no area.
	 */
	bool			FindArea(BNode& node, const std::string& language, place::Area* area);
	status_t		ChoosePlace(const std::vector<place::Hit>& hits, const std::string& wanted, const place::Position* near, double maxKilometres, place::Hit* chosen);
	/**
	 * What a Location, a photo or another file that is no Place is "located near": the Place that Nominatim found for it, and the Places on this computer that are near its position (within 250 metres,
	 * and no larger than 2 km across: a district or a city that contains it is reached through the place). The Place that was found is the file of a Place that has its link to OpenStreetMap
	 * (or Wikidata), else it is made in the Library with what Nominatim says and enriched later (picture, outline) in the background. The relations `located near` are stored, the same as ones
	 * that the user makes by hand.
	 */
	/** @param withNearby also relate the file to the places that are near it on this computer (a photo is; a person that lives in a place is related to that place only)
	 * @param label the label of the relation to the place that was found (the text that was looked up) */
	status_t		RelatePlace(const entry_ref& file, const place::Hit& hit, const std::string& description, bool withNearby = true, const std::string& label = std::string());
	void			ShowError(const char* message);
	void			PrintUsage(const char* error = NULL);
	/** the settings as they are now (the file, the settings of SEN that all plugins share): the instance may have been started before the user changed them; what the command line gave is kept */
	void			_ReloadSettings();

	place::Settings			fSettings;
	place::SettingsProvider	fSettingsProvider;
	sensei::SettingsSession	fSession;
	MappingUtil*			fMapper;
	BaseEnricher*			fHttp;
	sensei::DiskCache*		fCache;
	sensei::RateLimiter*		fLimiter;
	std::string				fLanguage;			///< what is asked for: the setting, else the language of the system
	std::string				fWanted;
	std::string				fQuery;			///< the message gave a text to look up (`query`): the place of that name is made and related to the file, the file itself is not written
	bool					fDebug;
	bool					fLanguageGiven;
	bool					fPositionGiven;		///< the command line allows the lookup by position (--position)
	bool					fNeedsConsent;		///< the file has a position that was not used because the user has not allowed it (to say so)
	bool					fOverwrite;
	std::string				fNamedPlace;		///< the name of the place that is looked up with its address (the title, else the name of the file)
	bool					fStreetOnly;		///< the file has a name and an address, and only the street was found: what the street has of its own is not written to the file
	bool					fAuto;
	int32					fExitCode;
};
