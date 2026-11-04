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
#include "MovieSettingsForm.h"
#include "MovieTmdb.h"

class MappingUtil;

/**
 * Akari, the movie enricher: looks a movie up at The Movie Database (the title and year from the attributes or the name of the file, or the TMDB link that an earlier run wrote),
 * writes its attributes and the poster to the file, and makes people of the director, the crew and the cast with a Contribution to the movie.
 *
 * This product uses the TMDB API but is not endorsed or certified by TMDB.
 */
class App : public BApplication
{
public:
	virtual void	AboutRequested();
					App();
	virtual			~App();
	virtual void	RefsReceived(BMessage* message);
	virtual void	ArgvReceived(int32 argc, char** argv);
	/** the settings window of SEN asks for the settings and sends the changes (see MovieSettingsForm.h) */
	virtual void	MessageReceived(BMessage* message);
	/** ends the application when a settings session is over */
	virtual void	Pulse();

	/** 0, or 1 if something went wrong (a cancelled choice is no error) */
	int32			ExitCode() const { return fExitCode; }

private:
	status_t		Enrich(BMessage* message, BMessage* reply);
	/** asks TMDB; B_PERMISSION_DENIED for a key that is not accepted, B_ERROR for anything else that went wrong */
	status_t		Get(const std::string& url, std::string* body);
	/** the movie that is meant: by the TMDB link that the file has, else by a search (and the user's choice if there are several) */
	status_t		FindMovie(const entry_ref& file, int64_t* id);
	status_t		ChooseMovie(const std::vector<movie::SearchHit>& hits, const std::string& title, int year, int64_t* id);
	void			ShowError(const char* message);
	void			PrintUsage(const char* error = NULL);
	/** the settings as they are now (the file, the settings of SEN that all plugins share, the variable of the key): the instance may have been started before the user changed them;
	 *  what the command line gave (the key, the language) is kept */
	void			_ReloadSettings();

	movie::Settings			fSettings;
	movie::SettingsProvider	fSettingsProvider;
	sensei::SettingsSession	fSession;
	MappingUtil*			fMapper;
	BaseEnricher*			fHttp;
	std::string				fWanted;
	bool					fDebug;
	bool					fKeyGiven;			///< the command line has the key
	bool					fLanguageGiven;
	bool					fOverwrite;
	bool					fAuto;
	int32					fExitCode;
};
