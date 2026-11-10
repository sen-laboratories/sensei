/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

namespace robin {

/**
 * The settings of Robin, the person enricher: lines of `key = value` in `~/config/settings/sen/robin.settings` (`#` starts a comment), changed in the settings window of SEN (Plugins, Enrich,
 * SEN Robin). They say which services Robin may ask about a person, and what it does with a photo. Plain C++.
 *
 * Whatever a service is asked is the hash of an e-mail address, the name of an account that the file links to, or the address of a homepage; a service that is switched off is never asked.
 */
struct Settings {
	/** Libravatar and Gravatar, by the SHA-256 hash of the e-mail address of the person (never the address): photo, and from Gravatar the profile and the accounts that the person verified */
	bool emailServices = true;
	/**
	 * GitHub, the Fediverse (ActivityPub: WebFinger, the actor, the outbox: a handle `@user@host` or a link to a profile on a server of it) and Bluesky: for the accounts that the links of a person name.
	 * They are switched in the list of the services (one list: a service that is off there is neither followed nor searched); the keys `github`, `fediverse` and `bluesky` are kept in step with it.
	 */
	bool github = true;
	bool fediverse = true;
	bool bluesky = true;
	/** look at the homepage of the person for the address of its feed (`<link rel="alternate">`), and read the feed of the person (its posts) */
	bool feeds = true;
	/** write the photo into the file as its thumbnail (`Media:Thumbnail`, with where it is from and its licence) */
	bool photo = true;
	/**
	 * Fill the attributes of the People application of Haiku (`META:name`, `META:nickname`) with the name and the nickname, where they are empty, so that the columns of the People window show them:
	 * `off`, `legacy` (the default: only the contact files of that application, `application/x-person`) or `all` (every person file, also the Person of SEN).
	 * Only where the attribute is empty: what is there is never replaced.
	 */
	std::string bePeople = "legacy";
	/**
	 * An account (Mastodon, GitHub...) spells the name of the person a little differently from the file ("Scot" for "Scott"): `ask` (the default: a question; Enrich (Overwrite) takes it), `always` (the name and
	 * the name of the file are corrected, without a question) or `never`.
	 */
	std::string nameCorrection = "ask";
	/** the longer side of the photo, in pixels: a larger photo is made smaller before it is written (a thumbnail is a few hundred pixels, not a megabyte) */
	int photoSize = 256;

	/**
	 * The services that are searched for a person by the name and the handle that the file has (the hunt), in the order that is set: `bluesky`, `mastodon`, `github`, `wikidata`, `orcid`. The line
	 * `hunt = bluesky mastodon -github wikidata orcid` keeps the places of all of them, a service with a `-` is off. Without the line all are on, in this order.
	 */
	std::vector<std::string>	huntOrder;			///< all the services in the order of the list, also those that are off (empty: the default order)
	std::vector<std::string>	huntOn;				///< those that are on, in order
	bool						huntSet = false;	///< the file has a line `hunt`
	/** the servers of the Fediverse whose search for accounts is asked (a Mastodon server answers for the accounts that it knows) */
	std::vector<std::string>	mastodonServers = {"mastodon.social"};

	/** every service of the hunt, the ones that are on and the ones that are off, in the order that is set */
	std::vector<std::string>	HuntOrder() const;
	/** the services that are asked, in order */
	std::vector<std::string>	HuntServices() const;
};

/** the services of the hunt in their default order */
const std::vector<std::string>& AllHuntServices();
/** a short description of a service for the list in the settings, and its name */
const char* HuntServiceName(const std::string& id);
const char* HuntServiceInfo(const std::string& id);

const int kMinPhotoSize = 64;
const int kMaxPhotoSize = 512;

/** Reads `key = value` lines into the settings; unknown keys and bad values are ignored (what is there stays). */
void ParseSettings(const std::string& text, Settings* settings);

/** Whether a value can be the value of a setting, so that the settings window can say what is wrong; `reason` gets it. */
bool ValidateSetting(const std::string& key, const std::string& value, std::string* reason = nullptr);

}	// namespace robin
