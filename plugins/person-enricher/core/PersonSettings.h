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
 * The services are one list, in the order of the priority: Robin asks every service that is on, in this order, and what an earlier one gave (and what the file had before) is not replaced by a later one
 * (the photo is the exception: the best one is kept). A service is asked for the account that the file names (a link, a handle, an e-mail address, a homepage), else for people of that name (the search).
 * A service that is switched off is never asked.
 */
struct Settings {
	/**
	 * The switches of the services that the list below gives (they are set from it when the settings are read; a program may switch one off here, too): Libravatar and Gravatar, by the SHA-256 hash of the
	 * e-mail address of the person (never the address); GitHub, the Fediverse (ActivityPub: WebFinger, the actor, the outbox: a handle `@user@host` or a link to a profile on a server of it) and Bluesky,
	 * for the account that the file names; the homepage and the feeds (the posts).
	 */
	bool emailServices = true;
	bool github = true;
	bool fediverse = true;
	bool bluesky = true;
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
	 * The services in the order of their priority: `bluesky`, `mastodon`, `github`, `wikidata`, `orcid`, `email`, `feeds`. The line `services = bluesky mastodon -github wikidata orcid email feeds` keeps
	 * the places of all of them, a service with a `-` is off. Without the line all are on, in this order.
	 */
	std::vector<std::string>	serviceOrder;			///< all the services in the order of the list, also those that are off (empty: the default order)
	std::vector<std::string>	serviceOn;				///< those that are on, in order
	bool						serviceSet = false;		///< the file has a line `services`
	/** the servers of the Fediverse whose search for accounts is asked (a Mastodon server answers for the accounts that it knows) */
	std::vector<std::string>	mastodonServers = {"mastodon.social"};

	/** every service, the ones that are on and the ones that are off, in the order that is set */
	std::vector<std::string>	ServiceOrder() const;
	/** whether a service is asked at all: it is on in the list, and not switched off in the flag of the service */
	bool						Uses(const std::string& id) const;
	/** the services that are searched for people by name (the hunt: not the e-mail address and the homepage), in order, as far as they are on */
	std::vector<std::string>	HuntServices() const;
};

/** all the services in their default order (the order of the list) */
const std::vector<std::string>& AllServices();
/** the services that can be searched for people by name, in their default order */
const std::vector<std::string>& AllHuntServices();
/** the name of a service and a short description of it for the list in the settings (empty for what is no service) */
const char* ServiceName(const std::string& id);
const char* ServiceInfo(const std::string& id);

const int kMinPhotoSize = 64;
const int kMaxPhotoSize = 512;

/** Reads `key = value` lines into the settings; unknown keys and bad values are ignored (what is there stays). */
void ParseSettings(const std::string& text, Settings* settings);

/** Whether a value can be the value of a setting, so that the settings window can say what is wrong; `reason` gets it. */
bool ValidateSetting(const std::string& key, const std::string& value, std::string* reason = nullptr);

}	// namespace robin
