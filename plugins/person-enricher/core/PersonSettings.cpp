/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PersonSettings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "../../../common/SettingsText.h"

namespace robin {

namespace st = sensei::settingstext;

namespace {

bool
IsBoolKey(const std::string& key)
{
	return key == "email_services" || key == "github" || key == "fediverse" || key == "bluesky" || key == "feeds" || key == "photo";
}


// a whole number in the range; false for anything else (a number with a fraction, text, nothing)
bool
ParseSize(const std::string& value, int* number)
{
	const std::string v = st::Trim(value);
	if (v.empty() || v.size() > 6)
		return false;
	for (char c : v) {
		if (c < '0' || c > '9')
			return false;
	}
	*number = atoi(v.c_str());
	return *number >= kMinPhotoSize && *number <= kMaxPhotoSize;
}

}	// namespace


const std::vector<std::string>&
AllHuntServices()
{
	static const std::vector<std::string> services = {"bluesky", "mastodon", "github", "wikidata", "orcid"};
	return services;
}


const char*
HuntServiceName(const std::string& id)
{
	if (id == "bluesky")
		return "Bluesky";
	if (id == "mastodon")
		return "Mastodon (Fediverse)";
	if (id == "github")
		return "GitHub";
	if (id == "wikidata")
		return "Wikidata";
	if (id == "orcid")
		return "ORCID";
	return "";
}


const char*
HuntServiceInfo(const std::string& id)
{
	if (id == "bluesky")
		return "People on Bluesky, by name or handle (a domain is a handle); photo and bio";
	if (id == "mastodon")
		return "Accounts of Mastodon servers, by name or handle (user@server); photo and bio";
	if (id == "github")
		return "Developers, by name or login; photo, bio, place, company, homepage";
	if (id == "wikidata")
		return "Well-known people; photo, description, ORCID and the accounts that are known";
	if (id == "orcid")
		return "Researchers, by name; bio and links";
	return "";
}


std::vector<std::string>
Settings::HuntOrder() const
{
	std::vector<std::string> order;
	for (const std::string& id : huntOrder) {
		if (*HuntServiceName(id) != '\0' && std::find(order.begin(), order.end(), id) == order.end())
			order.push_back(id);
	}
	for (const std::string& id : AllHuntServices()) {
		if (std::find(order.begin(), order.end(), id) == order.end())
			order.push_back(id);
	}
	return order;
}


std::vector<std::string>
Settings::HuntServices() const
{
	// (a service that the keys github, fediverse or bluesky switched off is off in the list, too: one list)
	auto allowed = [this](const std::string& id) {
		return (id != "github" || github) && (id != "mastodon" || fediverse) && (id != "bluesky" || bluesky);
	};
	std::vector<std::string> on;
	for (const std::string& id : HuntOrder()) {
		if (!allowed(id))
			continue;
		if (!huntSet || std::find(huntOn.begin(), huntOn.end(), id) != huntOn.end())
			on.push_back(id);
	}
	return on;
}


void
ParseSettings(const std::string& text, Settings* settings)
{
	for (const auto& pair : st::Pairs(text)) {
		const std::string& key = pair.first;
		const std::string& value = pair.second;
		if (key == "email_services")
			settings->emailServices = st::ParseBool(value, settings->emailServices);
		else if (key == "github")
			settings->github = st::ParseBool(value, settings->github);
		else if (key == "fediverse")
			settings->fediverse = st::ParseBool(value, settings->fediverse);
		else if (key == "bluesky")
			settings->bluesky = st::ParseBool(value, settings->bluesky);
		else if (key == "feeds")
			settings->feeds = st::ParseBool(value, settings->feeds);
		else if (key == "photo")
			settings->photo = st::ParseBool(value, settings->photo);
		else if (key == "be_people") {
			const std::string v = st::Trim(value);
			if (v == "off" || v == "legacy" || v == "all")
				settings->bePeople = v;
		} else if (key == "name_correction") {
			const std::string v = st::Trim(value);
			if (v == "ask" || v == "always" || v == "never")
				settings->nameCorrection = v;
		}
		else if (key == "hunt") {
			// (`name` is on, `-name` is off and keeps its place; a name that is none of the services is left out)
			settings->huntSet = true;
			settings->huntOrder.clear();
			settings->huntOn.clear();
			std::string word;
			for (size_t i = 0; i <= value.size(); i++) {
				if (i < value.size() && !std::isspace((unsigned char)value[i])) {
					word.push_back(value[i]);
					continue;
				}
				if (word.empty())
					continue;
				const bool off = word[0] == '-';
				const std::string id = off ? word.substr(1) : word;
				word.clear();
				if (*HuntServiceName(id) == '\0')
					continue;
				settings->huntOrder.push_back(id);
				if (!off)
					settings->huntOn.push_back(id);
			}
		} else if (key == "mastodon_servers") {
			std::vector<std::string> servers;
			std::string word;
			for (size_t i = 0; i <= value.size(); i++) {
				if (i < value.size() && !std::isspace((unsigned char)value[i])) {
					word.push_back(value[i]);
					continue;
				}
				if (!word.empty() && word.find('.') != std::string::npos && word.find_first_of("/:@") == std::string::npos)
					servers.push_back(word);
				word.clear();
			}
			if (!servers.empty())
				settings->mastodonServers = servers;
		} else if (key == "photo_size") {
			int size = 0;
			if (ParseSize(value, &size))
				settings->photoSize = size;
		}
	}
	// one list of the services: what is off in it is off for the accounts of the links, too
	if (settings->huntSet) {
		auto in = [settings](const char* id) { return std::find(settings->huntOn.begin(), settings->huntOn.end(), id) != settings->huntOn.end(); };
		settings->github = settings->github && in("github");
		settings->fediverse = settings->fediverse && in("mastodon");
		settings->bluesky = settings->bluesky && in("bluesky");
	}
}


bool
ValidateSetting(const std::string& key, const std::string& value, std::string* reason)
{
	auto fail = [reason](const char* text) {
		if (reason != nullptr)
			*reason = text;
		return false;
	};

	const std::string v = st::Trim(value);
	if (v.find_first_of("#\n\r") != std::string::npos)
		return fail("a # or a line break cannot be stored in the settings file");
	if (IsBoolKey(key))
		return st::IsBool(v) ? true : fail("yes or no");
	if (key == "hunt" || key == "mastodon_servers")
		return true;
	if (key == "be_people")
		return (v == "off" || v == "legacy" || v == "all") ? true : fail("off, legacy or all");
	if (key == "name_correction")
		return (v == "ask" || v == "always" || v == "never") ? true : fail("ask, always or never");
	if (key == "photo_size") {
		int size = 0;
		return ParseSize(v, &size) ? true : fail("a whole number of pixels from 64 to 512");
	}
	return fail("no such setting");
}

}	// namespace robin
