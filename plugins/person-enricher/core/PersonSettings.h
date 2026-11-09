/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

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
	/** GitHub, for a link to an account of it in `schema:sameAs` */
	bool github = true;
	/** the Fediverse (ActivityPub: WebFinger, the actor, the outbox), for a handle `@user@host` or a link to a profile on a server of it */
	bool fediverse = true;
	/** Bluesky, for a link to a profile on it */
	bool bluesky = true;
	/** look at the homepage of the person for the address of its feed (`<link rel="alternate">`), and read the feed of the person (its posts) */
	bool feeds = true;
	/** write the photo into the file as its thumbnail (`Media:Thumbnail`, with where it is from and its licence) */
	bool photo = true;
	/** the longer side of the photo, in pixels: a larger photo is made smaller before it is written (a thumbnail is a few hundred pixels, not a megabyte) */
	int photoSize = 256;
};

const int kMinPhotoSize = 64;
const int kMaxPhotoSize = 512;

/** Reads `key = value` lines into the settings; unknown keys and bad values are ignored (what is there stays). */
void ParseSettings(const std::string& text, Settings* settings);

/** Whether a value can be the value of a setting, so that the settings window can say what is wrong; `reason` gets it. */
bool ValidateSetting(const std::string& key, const std::string& value, std::string* reason = nullptr);

}	// namespace robin
