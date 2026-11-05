<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
<p align="center">
  <img src="images/akari-logo.jpg" alt="Akari: a film projector that throws light onto a file, and a graph of its people appears" width="320" />
</p>

<h1 align="center">Akari, the SEN movie enricher</h1>

*Akari* is Japanese for light: the projector in the logo throws light onto a file, and the graph of its people and relations appears. The folder and the signature keep the name of what the plugin does (`movie-enricher`) like the other plugins; Akari is its name for people. The application icon is `images/akari-icon.svg` (made by `images/akari-icon.py`).

Looks a movie up at [The Movie Database](https://www.themoviedb.org) (TMDB) and writes what it finds to the file, and makes **people** of the director, the crew and the cast, each with a
**Contribution** to the movie.

> This product uses the TMDB API but is not endorsed or certified by TMDB. The data and the images are TMDB's.

**You need a key.** TMDB gives one for free: register at themoviedb.org, then *Settings*, *API*, and copy the *API Key* (the v3 one, 32 characters) into the Settings window of SEN
(*Settings…* in the Deskbar menu, *Plugins*, *Enrich*, *SEN Akari*). Without one the plugin says so and does nothing.

**The movie.** The title and year come from the attributes of the file (`dc:title`, `schema:datePublished`), else from its name: `The.Matrix.1999.1080p.BluRay.x264-YIFY.mkv` and
`Blade Runner 2049 (2017).mp4` are found as *The Matrix* (1999) and *Blade Runner 2049* (2017). If TMDB finds **more than one movie**, the user chooses in a window
(`sen::ChooseCandidate`: the best first, rated by the title, the year, TMDB's order and how well known the movie is); `--auto` takes the best without asking, cancelling changes nothing.
A movie that was enriched before has its TMDB link (`schema:sameAs`) and is not searched again.

The attributes follow schema.org (the ontology *SEN Movies*): `dc:title`, `schema:alternateName` (the original title), `schema:datePublished`, `schema:duration` (ISO 8601: `PT2H16M`),
`dc:description`, `schema:genre`, `schema:inLanguage`, `schema:countryOfOrigin`, and `schema:sameAs` with the links to TMDB, IMDb, Wikidata and Letterboxd (`letterboxd.com/tmdb/<id>`
goes to the page of the film). The poster is the thumbnail of the file (`Media:Thumbnail`). A video that was enriched gets the semantic type *Movie* (`META:TYPE`), so that Shoji shows
its form; its type for Haiku stays. What the file has already is kept unless the action is *Enrich (Overwrite)*.

**The people.** For each credit a person is made (a file of the type *Person* in the **Library** of the current context, `~/Library/People`; not next to the movie) with the name, the
birthday, the biography, the links (TMDB, IMDb, Wikidata) and the photo as the thumbnail. The person has a **Contribution** relation to the movie (core ontology) for every credit, with
the **role** (`SEN:REL:Role`: Director, Writer, Producer, Composer, Cinematographer, Editor, Actor), for an actor the **character** (`schema:characterName`), and the **place** in the
credits (`schema:position`). A person who did two things has two Contributions (one for each role: when TMDB changes a credit, its place or the character, the Contribution of that role is replaced when the movie is enriched again, not added to). A person that exists (found by the TMDB link, or by the name) is updated and not made twice.

## Settings

The Settings window of SEN shows them; they are kept in `~/config/settings/sen/akari.settings` (lines of `key = value`, `#` starts a comment; comments and other lines stay when the window changes a value).

| Key | Default | |
|---|---|---|
| `api_key` | - | your TMDB API key (v3) or API Read Access Token (v4, three parts with dots; sent in a header, not in the address), both from themoviedb.org/settings/api. The key is part of the addresses that are asked: it is never printed to the log. `SEN_TMDB_KEY` or `--key` override it for one run |
| `language` | `en-US` | the language of titles, descriptions and biographies (`de-DE`, `fr-FR`, ...) |
| `crew` | yes | make people of the director and the crew (writers, producers, composer, cinematography, editing) |
| `cast` | yes | make people of the cast |

Asking which movie if several are found, how many actors are made (the first ones of the credits) and where the people go (files or attributes) are not Akari's: they are set once for all plugins in the Settings window of SEN, under **Plugins**.

## On the command line

```sh
SenMovieEnricher [--auto] [--wipe] [--key <key>] [--language <l>] [--debug] <file>...
```

## For developers

* `core/` has no Haiku API and is tested everywhere (`tests/run.sh`, needs the nlohmann/json headers): the guess of title and year from a file name (`MovieName`), the addresses, the
  answers of TMDB and the scores (`MovieTmdb`), and the settings (`MovieSettings`).
* `tests/people.sh` (in a Haiku VM, with a SEN server) tests what is written to the file and the people with their Contributions, with answers of TMDB made up in the test.
* The settings protocol is the one of all plugins (`common/SettingsSession.h`, `tests/SettingsSessionTest.cpp`).
