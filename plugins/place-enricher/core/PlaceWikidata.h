/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

/**
 * What the plugin needs of Wikidata (https://www.wikidata.org) and Wikimedia Commons (https://commons.wikimedia.org) for the picture of a place: the short description of the place in the
 * language of the user, which image of it Wikidata names (a photo, the coat of arms, the flag, a locator map), and the size, the license and the author of that image at Commons.
 * No network and no Haiku here: the answers are the JSON text.
 *
 * Wikidata is in the public domain (CC0). The images at Commons have the license that their author gave them; a picture that asks to name its author (CC BY, CC BY-SA) is credited with
 * CreditOf() wherever it is kept. Wikimedia wants an identifying User-Agent.
 */
namespace place {

/** What a Wikidata item says of a place. */
struct Entity {
	std::string	id;				///< `Q5943`
	std::string	label;			///< the name in the language that was asked for
	std::string	description;	///< the short description: `Kathedrale des Erzbistums Wien`
	std::string	photo;			///< P18: image, the name of a file at Commons (without `File:`)
	std::string	coatOfArms;		///< P94
	std::string	flag;			///< P41
	std::string	locatorMap;		///< P242
	std::string	seal;			///< P158
	std::string	logo;			///< P154
};

/** An image at Commons as the thumbnail of a place. */
struct Image {
	std::string	file;				///< the name of the file at Commons
	std::string	kind;				///< photo, coat of arms, flag, locator map, seal, logo
	std::string	thumbnailUrl;		///< the image in the size that was asked for (always a PNG or JPEG, also for an SVG)
	std::string	pageUrl;			///< the page of the file at Commons (the license and the author are there)
	std::string	mime;
	std::string	license;			///< `CC BY-SA 4.0`, `Public domain`
	std::string	licenseUrl;
	std::string	artist;				///< the author, as plain text
	std::string	credit;				///< what Commons says as the credit, as plain text
	bool		attributionRequired = false;
};

// ---- the addresses
/** The item with its claims, descriptions and labels in @p language (and English if there is none). */
std::string EntityUrl(const std::string& wikidataId, const std::string& language);
/** The information on a file at Commons, with a thumbnail of the width @p width. */
std::string ImageInfoUrl(const std::string& file, int width);

// ---- the answers
/** False if the text is not the answer for @p id (or the item is missing). */
bool ParseEntity(const std::string& json, const std::string& id, const std::string& language, Entity* entity);
/** False if the text has no image (the file does not exist). */
bool ParseImageInfo(const std::string& json, Image* image);

// ---- what to make of them
/**
 * The image that stands for a place: for an administrative area (a city, a state, a country) the coat of arms, else the flag, the locator map, the seal, the photo; for any other place the
 * photo, else the logo. An empty Image (no file) if Wikidata names none of them.
 */
Image PickImage(const Entity& entity, bool administrative);
/** The credit that the license of the image asks to say, as one line: `C.Stadler/Bwag, CC BY-SA 4.0 (Wikimedia Commons)`; for an image without author `Public domain (Wikimedia Commons)`. */
std::string CreditOf(const Image& image);
/** Text of HTML without the tags and with the entities decoded (the author at Commons is a link): `<a href="...">Name</a>` is `Name`. */
std::string StripHtml(const std::string& html);

}	// namespace place
