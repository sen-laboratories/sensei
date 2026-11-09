/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PersonData.h"
#include "PersonEnricher.h"
#include "PersonSettings.h"

namespace robin {

/**
 * One answer of a service to the question "who is this person?": an account, a record, an entry. Robin shows several of them to choose from (the same dialog as the books and the
 * places), each with the score of how well it fits what the file says.
 */
struct HuntHit {
	std::string	service;		///< `bluesky`, `mastodon`, `github`, `wikidata` or `orcid`
	std::string	id;				///< the handle, the login, the Q number, the ORCID iD
	std::string	name;			///< as the service calls the person
	std::string	url;			///< the page of the account on the web: it goes into `schema:sameAs`
	std::string	description;	///< the bio, the description
	std::string	location;
	std::string	company;
	std::string	jobTitle;
	std::string	homepage;
	std::string	avatarUrl;
	std::string	orcid;			///< the ORCID iD, where the hit has one (a Wikidata entry, a GitHub account that says it)
	std::string	wikidata;		///< the Q number
	std::vector<std::string> links;	///< other pages that the hit names for the same person (a website, other accounts)
	int64_t		followers = 0;
	float		score = 0;		///< 0 to 1, how well it fits what the file says
};

/** A handle as people write it: with or without the `@`, a name on a server of the Fediverse (`user@host`), a domain (Bluesky: `alice.bsky.social`, `sen-labs.org`), or a plain name. */
struct Handle {
	enum Kind { kNone, kFediverse, kDomain, kPlain };
	Kind		kind = kNone;
	std::string	user;			///< of a Fediverse handle; else the whole handle
	std::string	host;			///< of a Fediverse handle
	std::string	text;			///< the handle as it is written back into the file: `@user@host` for the Fediverse, the domain or the name as it is, without an `@`
};

/**
 * Finds out who a person is on the public services that people put themselves on deliberately: Bluesky (whose handles are domains: `sen-labs.org` is one), the Fediverse (Mastodon), GitHub,
 * Wikidata and ORCID, in the order that the settings give and as far as they are switched on. What the file already says (the name, the handle, the links, the place, the company) is the
 * filter: a hit that fits it scores higher. Nothing is written here: the caller picks, or asks the user to.
 */
class Hunter {
public:
	Hunter(PersonEnricher::HttpGetFunc httpGet, const Settings& settings);

	/** All hits of the enabled services, scored and sorted, the best first. Nothing for a person with neither a name nor a handle. */
	std::vector<HuntHit> Search(const PersonProfile& wanted);

	/** Whether the best hit is so much better than the others that nobody has to be asked: from 0.8, and 0.25 ahead of the next. */
	static bool IsClearWinner(const std::vector<HuntHit>& hits);

	// ---- what the tests look at

	/** `@alice@m.example`, `alice@m.example`, `@alice.bsky.social`, `sen-labs.org`, `alice`: what kind, and the form that is written back. */
	static Handle ParseHandle(const std::string& text);
	/** 0 to 1: how well a name fits another (accents and case do not count, the order of the words does not, a missing middle name costs little). */
	static float NameSimilarity(const std::string& a, const std::string& b);
	/** The score of a hit for a person; also stores it in the hit. */
	static float Score(HuntHit* hit, const PersonProfile& wanted);
	/** `a b` as `a%20b`: for the query of an address. */
	static std::string UrlEncode(const std::string& text);

	static bool ParseBlueskySearch(const std::string& json, std::vector<HuntHit>* hits);
	static bool ParseBlueskyProfile(const std::string& json, HuntHit* hit);
	static bool ParseMastodonSearch(const std::string& json, const std::string& host, std::vector<HuntHit>* hits);
	static bool ParseMastodonAccount(const std::string& json, const std::string& host, HuntHit* hit);
	static bool ParseGitHubSearch(const std::string& json, std::vector<HuntHit>* hits);
	static bool ParseGitHubUser(const std::string& json, HuntHit* hit);
	static bool ParseWikidataSearch(const std::string& json, std::vector<std::string>* ids);
	static bool ParseWikidataEntities(const std::string& json, std::vector<HuntHit>* hits);
	static bool ParseOrcidSearch(const std::string& json, std::vector<std::string>* ids);
	static bool ParseOrcidPerson(const std::string& json, const std::string& id, HuntHit* hit);

private:
	bool _Get(const std::string& url, const char* accept, std::string* body);
	void _SearchBluesky(const PersonProfile& wanted, std::vector<HuntHit>* hits);
	void _SearchMastodon(const PersonProfile& wanted, std::vector<HuntHit>* hits);
	void _SearchGitHub(const PersonProfile& wanted, std::vector<HuntHit>* hits);
	void _SearchWikidata(const PersonProfile& wanted, std::vector<HuntHit>* hits);
	void _SearchOrcid(const PersonProfile& wanted, std::vector<HuntHit>* hits);

	PersonEnricher::HttpGetFunc fHttpGet;
	Settings fSettings;
};

}	// namespace robin
