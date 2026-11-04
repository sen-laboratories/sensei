/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * What the plugin needs of The Movie Database (https://www.themoviedb.org, API v3): the addresses to ask, and what the answers hold. No network and no Haiku here: the
 * answers are the JSON text, so everything can be tested on any computer.
 *
 * This product uses the TMDB API but is not endorsed or certified by TMDB.
 */
namespace movie {

/** One answer of the search for a title. */
struct SearchHit {
	int64_t		id = 0;
	std::string	title;
	std::string	originalTitle;
	int			year = 0;
	std::string	overview;
	std::string	posterPath;
	double		popularity = 0;
	int			voteCount = 0;
};

/** A person in the credits of a movie. */
struct Credit {
	int64_t		personId = 0;
	std::string	name;
	std::string	role;			///< Director, Writer, Producer, Composer, Cinematographer, Editor, Actor
	std::string	character;		///< of an actor
	int			position = 0;	///< the place among those with the role, from 1 (an actor: the billing order)
	std::string	profilePath;
};

/** A movie as TMDB describes it. */
struct Details {
	int64_t		id = 0;
	std::string	title;
	std::string	originalTitle;
	std::string	releaseDate;			///< `1999-03-30`, empty if not known
	int			runtime = 0;			///< minutes
	std::string	overview;
	std::vector<std::string> genres;
	std::vector<std::string> languages;	///< codes, the original language first
	std::vector<std::string> countries;	///< codes
	std::string	imdbId;
	std::string	wikidataId;
	std::string	posterPath;
	std::vector<Credit> cast;			///< in the order of the credits
	std::vector<Credit> crew;			///< only the jobs of KeyCrew()
};

/** A person as TMDB describes them. */
struct PersonDetails {
	int64_t		id = 0;
	std::string	name;
	std::string	biography;
	std::string	birthday;				///< `1964-09-02`
	std::string	imdbId;
	std::string	wikidataId;
	std::string	profilePath;
};

// ---- the addresses (the key is the API key of the user, v3: it is part of the address, so never print one)
std::string SearchUrl(const std::string& key, const std::string& language, const std::string& title, int year);
std::string DetailsUrl(const std::string& key, const std::string& language, int64_t id);
std::string PersonUrl(const std::string& key, const std::string& language, int64_t id);
/** The address of an image, `size` is w185 (a photo), w500 (a poster); the path is what TMDB gives (with its first `/`). */
std::string ImageUrl(const std::string& size, const std::string& path);
/** Text for an address: everything but letters, digits and `-._~` as %XX. */
std::string PercentEncode(const std::string& text);
/** An address without the key, for logs: `api_key=...` is replaced by `api_key=***`. */
std::string WithoutKey(const std::string& url);

// ---- the answers
/** The hits of a search, best first as TMDB sorts them. False if the text is not an answer of TMDB. */
bool ParseSearch(const std::string& json, std::vector<SearchHit>* hits);
/** False if the text is not the details of a movie. The crew is cut down to the key jobs. */
bool ParseDetails(const std::string& json, Details* details);
bool ParsePerson(const std::string& json, PersonDetails* person);
/** The error that TMDB says (`Invalid API key`), empty if the text says none. */
std::string ParseError(const std::string& json);

// ---- what to make of them
/** The id that a list of links (`schema:sameAs`, the links in one string, any separator) has for TMDB: `https://www.themoviedb.org/<kind>/603` or `.../603-the-matrix`. 0 if there is none. */
int64_t TmdbIdFrom(const std::string& links, const std::string& kind);
/** `PT2H28M`; empty for no length. */
std::string DurationIso(int minutes);
/** Where else the movie is known: TMDB, IMDb, Wikidata, and Letterboxd (letterboxd.com/tmdb/<id> goes to the page of the film). */
std::vector<std::string> SameAs(const Details& details);
std::vector<std::string> SameAs(const PersonDetails& person);
/** Whether the title of a hit is the one that was asked for, or holds it or is held by it (compared without case, blanks and marks). Always true if nothing was asked for. */
bool TitleMatches(const SearchHit& hit, const std::string& wanted);
/**
 * How good a hit is for the movie that was asked for, from 0 to 1: the title (40%: the same is 1, one holds the other 0.6), the year (25%: the same is 1, one off 0.5, none asked 0.5),
 * the order of TMDB (15%) and how well known the movie is (20%). The weights are a guess that is easy to change.
 */
float ScoreHit(const SearchHit& hit, int position, int count, const std::string& wantedTitle, int wantedYear);
/** The key crew of a movie as credits: Director, Writer, Producer, Composer, Cinematographer, Editor; one for each person and role. */
std::vector<Credit> KeyCrew(const std::vector<Credit>& crew);

}	// namespace movie
