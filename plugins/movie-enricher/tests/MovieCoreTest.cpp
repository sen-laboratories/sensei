/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// Tests of core/: no Haiku needed. Build and run with tests/run.sh.

#include <cstdio>
#include <string>

#include "MovieName.h"
#include "MovieSettings.h"
#include "MovieTmdb.h"
#include "../../../common/SettingsText.h"

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

static void
TestName()
{
	using movie::GuessFromFileName;
	auto check = [](const char* file, const char* title, int year) {
		const movie::Guess guess = GuessFromFileName(file);
		const bool ok = guess.title == title && guess.year == year;
		if (!ok)
			std::printf("  '%s' -> '%s' %d (expected '%s' %d)\n", file, guess.title.c_str(), guess.year, title, year);
		return ok;
	};
	CHECK(check("The.Matrix.1999.1080p.BluRay.x264-YIFY.mkv", "The Matrix", 1999));
	CHECK(check("Blade Runner 2049 (2017).mp4", "Blade Runner 2049", 2017));
	CHECK(check("2001 A Space Odyssey (1968).avi", "2001 A Space Odyssey", 1968));
	CHECK(check("1917.2019.1080p.WEB-DL.mkv", "1917", 2019));
	CHECK(check("Amélie.mkv", "Amélie", 0));
	CHECK(check("Mr._Nobody_2009_720p.mkv", "Mr Nobody", 2009));
	CHECK(check("Dune - Part Two [2024] [YTS.MX].mp4", "Dune - Part Two", 2024));
	CHECK(check("Alien 1979 Directors Cut 1080p.mkv", "Alien", 1979));
	CHECK(check("Heat.1995", "Heat", 1995));								// (no extension)
	CHECK(check("Seven Samurai.mov", "Seven Samurai", 0));
	CHECK(check("blade.runner.1982.remastered.2160p.UHD.BluRay.x265.mkv", "blade runner", 1982));
	CHECK(check("My Holiday 2023.final.mp4", "My Holiday", 2023));				// (a year is a year)
	CHECK(check("Her (2013) 720p.mkv", "Her", 2013));
	CHECK(check("", "", 0));
	CHECK(check("1080p.mkv", "", 0));											// (a name that is only how it was made)
	CHECK(check("Movie.Without.Known.Extension.xyz", "Movie Without Known Extension xyz", 0));
}


// what TMDB answers (cut down to the fields that the plugin reads)
static const char* kSearch = R"({"page":1,"results":[
 {"id":603,"title":"The Matrix","original_title":"The Matrix","release_date":"1999-03-30","overview":"A hacker learns the truth.","popularity":85.3,"vote_count":25000,"poster_path":"/f89U3ADr1oiB1s9GkdPOEpXUk5H.jpg"},
 {"id":604,"title":"The Matrix Reloaded","original_title":"The Matrix Reloaded","release_date":"2003-05-15","overview":"","popularity":50.0,"vote_count":12000,"poster_path":null},
 {"id":99,"title":"Matrix","original_title":"Matrix","release_date":"","overview":"","popularity":1.0,"vote_count":3,"poster_path":null},
 {"title":"No id"}]})";

static const char* kDetails = R"({"id":603,"title":"The Matrix","original_title":"The Matrix","original_language":"en","release_date":"1999-03-30","runtime":136,
 "overview":"A hacker learns the truth.","poster_path":"/f89U3ADr1oiB1s9GkdPOEpXUk5H.jpg","imdb_id":"tt0133093",
 "genres":[{"id":28,"name":"Action"},{"id":878,"name":"Science Fiction"}],
 "spoken_languages":[{"iso_639_1":"en","name":"English"},{"iso_639_1":"fr","name":"Français"}],
 "production_countries":[{"iso_3166_1":"US","name":"United States of America"}],
 "external_ids":{"imdb_id":"tt0133093","wikidata_id":"Q83495"},
 "credits":{
  "cast":[{"id":6385,"name":"Carrie-Anne Moss","character":"Trinity","order":1,"profile_path":"/a.jpg"},
          {"id":6384,"name":"Keanu Reeves","character":"Thomas A. Anderson / Neo","order":0,"profile_path":"/b.jpg"},
          {"id":2975,"name":"Laurence Fishburne","character":"Morpheus","order":2,"profile_path":null}],
  "crew":[{"id":9340,"name":"Lana Wachowski","job":"Director","department":"Directing","profile_path":null},
          {"id":9339,"name":"Lilly Wachowski","job":"Director","department":"Directing","profile_path":null},
          {"id":9340,"name":"Lana Wachowski","job":"Screenplay","department":"Writing"},
          {"id":9339,"name":"Lilly Wachowski","job":"Writer","department":"Writing"},
          {"id":9340,"name":"Lana Wachowski","job":"Writer","department":"Writing"},
          {"id":1,"name":"Joel Silver","job":"Producer","department":"Production"},
          {"id":2,"name":"Don Davis","job":"Original Music Composer"},
          {"id":3,"name":"Bill Pope","job":"Director of Photography"},
          {"id":4,"name":"Zach Staenberg","job":"Editor"},
          {"id":5,"name":"Somebody","job":"Best Boy"}]}})";

static const char* kPerson = R"({"id":6384,"name":"Keanu Reeves","biography":"A Canadian actor.","birthday":"1964-09-02","imdb_id":null,"profile_path":"/b.jpg",
 "external_ids":{"imdb_id":"nm0000206","wikidata_id":"Q43416"}})";

static void
TestTmdb()
{
	using namespace movie;

	// the addresses: the text of a title is encoded, the key never appears in what is shown
	const std::string url = SearchUrl("abc123", "en-US", "Amélie & Co", 2001);
	CHECK(url == "https://api.themoviedb.org/3/search/movie?api_key=abc123&query=Am%C3%A9lie%20%26%20Co&include_adult=false&language=en-US&year=2001");
	CHECK(SearchUrl("k", "", "x", 0) == "https://api.themoviedb.org/3/search/movie?api_key=k&query=x&include_adult=false");
	CHECK(DetailsUrl("k", "de-DE", 603) == "https://api.themoviedb.org/3/movie/603?api_key=k&append_to_response=credits,external_ids&language=de-DE");
	CHECK(PersonUrl("k", "en-US", 6384).find("/person/6384?api_key=k&append_to_response=external_ids") != std::string::npos);
	CHECK(ImageUrl("w500", "/f89U.jpg") == "https://image.tmdb.org/t/p/w500/f89U.jpg" && ImageUrl("w185", "").empty() && ImageUrl("w185", "x.jpg") == "https://image.tmdb.org/t/p/w185/x.jpg");
	CHECK(WithoutKey(url).find("abc123") == std::string::npos && WithoutKey(url).find("api_key=***&query=") != std::string::npos);
	CHECK(WithoutKey("https://x/y?api_key=secret") == "https://x/y?api_key=***" && WithoutKey("https://x/y") == "https://x/y");
	CHECK(PercentEncode("a b/ü") == "a%20b%2F%C3%BC");

	// the search
	std::vector<SearchHit> hits;
	CHECK(ParseSearch(kSearch, &hits));
	CHECK(hits.size() == 3);			// (an answer without an id is no movie)
	CHECK(hits[0].id == 603 && hits[0].title == "The Matrix" && hits[0].year == 1999 && hits[0].voteCount == 25000 && hits[0].posterPath == "/f89U3ADr1oiB1s9GkdPOEpXUk5H.jpg");
	CHECK(hits[1].posterPath.empty() && hits[2].year == 0);
	CHECK(!ParseSearch("not json", &hits) && !ParseSearch("{\"page\":1}", &hits) && !ParseSearch("[]", &hits));
	CHECK(ParseError("{\"status_code\":7,\"status_message\":\"Invalid API key: You must be granted a valid key.\",\"success\":false}") == "Invalid API key: You must be granted a valid key.");
	CHECK(ParseError(kSearch).empty() && ParseError("junk").empty());

	// the scores: the movie that was asked for first
	CHECK(TitleMatches(hits[0], "the matrix") && TitleMatches(hits[1], "The Matrix") && !TitleMatches(hits[0], "Alien") && TitleMatches(hits[0], ""));
	const float matrix = ScoreHit(hits[0], 0, 3, "The Matrix", 1999);
	const float reloaded = ScoreHit(hits[1], 1, 3, "The Matrix", 1999);
	const float obscure = ScoreHit(hits[2], 2, 3, "The Matrix", 1999);
	CHECK(matrix > reloaded && reloaded > obscure && matrix <= 1.0f && obscure >= 0.0f);
	CHECK(ScoreHit(hits[0], 0, 3, "The Matrix", 1999) > ScoreHit(hits[0], 0, 3, "The Matrix", 2005));		// (a year that does not fit)
	CHECK(ScoreHit(hits[0], 0, 1, "", 0) > 0.0f);

	// the details
	Details d;
	CHECK(ParseDetails(kDetails, &d));
	CHECK(d.id == 603 && d.title == "The Matrix" && d.releaseDate == "1999-03-30" && d.runtime == 136 && d.imdbId == "tt0133093" && d.wikidataId == "Q83495");
	CHECK(d.genres.size() == 2 && d.genres[1] == "Science Fiction");
	CHECK(d.languages.size() == 2 && d.languages[0] == "en" && d.languages[1] == "fr");		// (the original language first, once)
	CHECK(d.countries.size() == 1 && d.countries[0] == "US");
	// the cast in the order of the credits, with the part
	CHECK(d.cast.size() == 3 && d.cast[0].name == "Keanu Reeves" && d.cast[0].character == "Thomas A. Anderson / Neo" && d.cast[0].position == 1 && d.cast[0].role == "Actor");
	CHECK(d.cast[2].name == "Laurence Fishburne" && d.cast[2].profilePath.empty());
	// the crew: the key jobs, in the order of the roles, a person once for a role
	CHECK(d.crew.size() == 8);
	CHECK(d.crew[0].role == "Director" && d.crew[0].name == "Lana Wachowski" && d.crew[0].position == 1);
	CHECK(d.crew[1].role == "Director" && d.crew[1].name == "Lilly Wachowski" && d.crew[1].position == 2);
	CHECK(d.crew[2].role == "Writer" && d.crew[2].personId == 9340 && d.crew[3].role == "Writer" && d.crew[3].personId == 9339 && d.crew[3].position == 2);
	CHECK(d.crew[4].role == "Producer" && d.crew[5].role == "Composer" && d.crew[6].role == "Cinematographer" && d.crew[7].role == "Editor");
	CHECK(!ParseDetails("{}", &d) && !ParseDetails("junk", &d));
	CHECK(ParseDetails("{\"id\":5,\"title\":\"Bare\",\"runtime\":null,\"imdb_id\":null,\"poster_path\":null,\"genres\":[]}", &d) && d.runtime == 0 && d.imdbId.empty() && d.cast.empty());

	// the person
	PersonDetails p;
	CHECK(ParsePerson(kPerson, &p));
	CHECK(p.name == "Keanu Reeves" && p.birthday == "1964-09-02" && p.imdbId == "nm0000206" && p.wikidataId == "Q43416" && p.profilePath == "/b.jpg");
	CHECK(!ParsePerson("{\"name\":\"x\"}", &p));

	// the id that links hold
	CHECK(TmdbIdFrom("https://www.themoviedb.org/movie/603;https://www.imdb.com/title/tt0133093/", "movie") == 603);
	CHECK(TmdbIdFrom("https://www.imdb.com/title/tt0133093/;https://www.themoviedb.org/movie/603-the-matrix", "movie") == 603);
	CHECK(TmdbIdFrom("https://www.themoviedb.org/person/6384", "movie") == 0 && TmdbIdFrom("https://www.themoviedb.org/person/6384", "person") == 6384);
	CHECK(TmdbIdFrom("", "movie") == 0 && TmdbIdFrom("https://www.themoviedb.org/movie/", "movie") == 0 && TmdbIdFrom("https://www.themoviedb.org/movie/x", "movie") == 0);

	// what is made of them
	CHECK(DurationIso(136) == "PT2H16M" && DurationIso(120) == "PT2H" && DurationIso(45) == "PT45M" && DurationIso(0).empty() && DurationIso(-5).empty());
	const std::vector<std::string> links = SameAs(d.id == 5 ? d : d);
	CHECK(links.size() >= 2 && links[0] == "https://www.themoviedb.org/movie/5");
	Details matrixDetails;
	ParseDetails(kDetails, &matrixDetails);
	const std::vector<std::string> matrixLinks = SameAs(matrixDetails);
	CHECK(matrixLinks.size() == 4 && matrixLinks[0] == "https://www.themoviedb.org/movie/603" && matrixLinks[1] == "https://www.imdb.com/title/tt0133093/"
		&& matrixLinks[2] == "https://www.wikidata.org/entity/Q83495" && matrixLinks[3] == "https://letterboxd.com/tmdb/603");
	const std::vector<std::string> personLinks = SameAs(p);
	CHECK(personLinks.size() == 3 && personLinks[0] == "https://www.themoviedb.org/person/6384" && personLinks[1] == "https://www.imdb.com/name/nm0000206/");
}

static void
TestSettings()
{
	movie::Settings s;
	CHECK(s.apiKey.empty() && s.language == "en-US" && s.choose && s.crew && s.cast && s.maxCast == 10);
	movie::ParseSettings("# movies\napi_key = 0123abcd   # my key\nlanguage = de-DE\nchoose = no\ncast = off\nmax_cast = 25\n", &s);
	CHECK(s.apiKey == "0123abcd" && s.language == "de-DE" && !s.choose && !s.cast && s.crew && s.maxCast == 25);
	movie::ParseSettings("api_key = has space\nlanguage = German\nmax_cast = 51\nmax_cast = x\nnonsense = 1\ncrew = maybe\n", &s);
	CHECK(s.apiKey == "0123abcd" && s.language == "de-DE" && s.maxCast == 25 && s.crew);

	std::string reason;
	CHECK(movie::ValidateSetting("api_key", "") && movie::ValidateSetting("api_key", "abc-123_X") && !movie::ValidateSetting("api_key", "a b") && !movie::ValidateSetting("api_key", "a&b"));
	CHECK(movie::ValidateSetting("language", "en") && movie::ValidateSetting("language", "pt-BR") && !movie::ValidateSetting("language", "english") && !movie::ValidateSetting("language", "en-us") && !movie::ValidateSetting("language", ""));
	CHECK(movie::ValidateSetting("max_cast", "0") && movie::ValidateSetting("max_cast", "50") && !movie::ValidateSetting("max_cast", "51") && !movie::ValidateSetting("max_cast", "-1"));
	CHECK(movie::ValidateSetting("cast", "yes") && !movie::ValidateSetting("cast", "maybe", &reason) && reason == "yes or no");
	CHECK(!movie::ValidateSetting("colour", "red", &reason) && reason == "no such setting" && !movie::ValidateSetting("choose", "yes # no"));
	// the file keeps its comments when a value changes
	const std::string changed = sensei::settingstext::Update("# movies\napi_key = old  # mine\n", {{"api_key", "new1"}, {"language", "fr-FR"}});
	CHECK(changed == "# movies\napi_key = new1 # mine\nlanguage = fr-FR\n");
}

static void
TestNames()
{
	movie::Credit a, b, c, d;
	a.name = "Second Actor"; a.role = "Actor"; a.position = 2;
	b.name = "First Actor"; b.role = "Actor"; b.position = 1;
	c.name = "Dir Ector"; c.role = "Director"; c.position = 1;
	d.name = "First Actor"; d.role = "Director"; d.position = 2;			// (the same name in another role; and twice as an actor below)
	std::vector<movie::Credit> credits = {a, b, c, d, b};
	const std::vector<std::string> actors = movie::NamesWithRole(credits, "Actor");
	CHECK(actors.size() == 2 && actors[0] == "First Actor" && actors[1] == "Second Actor");	// (by position, once each)
	const std::vector<std::string> directors = movie::NamesWithRole(credits, "Director");
	CHECK(directors.size() == 2 && directors[0] == "Dir Ector" && directors[1] == "First Actor");
	CHECK(movie::NamesWithRole(credits, "Writer").empty());
	CHECK(movie::NamesWithRole({}, "Actor").empty());
}

int
main()
{
	TestNames();
	TestName();
	TestTmdb();
	TestSettings();
	std::printf("%d checks, %d failed\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
