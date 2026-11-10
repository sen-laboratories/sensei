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
	return key == "photo";
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
AllServices()
{
	static const std::vector<std::string> services = {"bluesky", "mastodon", "github", "wikidata", "orcid", "email", "feeds"};
	return services;
}


const std::vector<std::string>&
AllHuntServices()
{
	static const std::vector<std::string> services = {"bluesky", "mastodon", "github", "wikidata", "orcid"};
	return services;
}


const char*
ServiceName(const std::string& id)
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
	if (id == "email")
		return "Gravatar and Libravatar";
	if (id == "feeds")
		return "Homepage and feeds";
	return "";
}


const char*
ServiceInfo(const std::string& id)
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
	if (id == "email")
		return "By the hash of the e-mail address (never the address): photo, profile, verified accounts";
	if (id == "feeds")
		return "The feed of the homepage and the accounts: the posts";
	return "";
}


std::vector<std::string>
Settings::ServiceOrder() const
{
	std::vector<std::string> order;
	for (const std::string& id : serviceOrder) {
		if (*ServiceName(id) != '\0' && std::find(order.begin(), order.end(), id) == order.end())
			order.push_back(id);
	}
	for (const std::string& id : AllServices()) {
		if (std::find(order.begin(), order.end(), id) == order.end())
			order.push_back(id);
	}
	return order;
}


bool
Settings::Uses(const std::string& id) const
{
	if (*ServiceName(id) == '\0')
		return false;
	if (serviceSet && std::find(serviceOn.begin(), serviceOn.end(), id) == serviceOn.end())
		return false;
	// (the switch of the service itself: a program may have it off while the list has it on)
	if (id == "email")
		return emailServices;
	if (id == "github")
		return github;
	if (id == "mastodon")
		return fediverse;
	if (id == "bluesky")
		return bluesky;
	if (id == "feeds")
		return feeds;
	return true;
}


std::vector<std::string>
Settings::HuntServices() const
{
	std::vector<std::string> on;
	for (const std::string& id : ServiceOrder()) {
		const std::vector<std::string>& searchable = AllHuntServices();
		if (std::find(searchable.begin(), searchable.end(), id) != searchable.end() && Uses(id))
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
		if (key == "photo") {
			settings->photo = st::ParseBool(value, settings->photo);
		} else if (key == "be_people") {
			const std::string v = st::Trim(value);
			if (v == "off" || v == "legacy" || v == "all")
				settings->bePeople = v;
		} else if (key == "name_correction") {
			const std::string v = st::Trim(value);
			if (v == "ask" || v == "always" || v == "never")
				settings->nameCorrection = v;
		} else if (key == "services") {
			// (`name` is on, `-name` is off and keeps its place; a name that is none of the services is left out)
			settings->serviceSet = true;
			settings->serviceOrder.clear();
			settings->serviceOn.clear();
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
				if (*ServiceName(id) == '\0')
					continue;
				if (std::find(settings->serviceOrder.begin(), settings->serviceOrder.end(), id) != settings->serviceOrder.end())
					continue;
				settings->serviceOrder.push_back(id);
				if (!off)
					settings->serviceOn.push_back(id);
			}
			// (a service that the line does not name is a new one: on, at the end)
			for (const std::string& id : AllServices()) {
				if (std::find(settings->serviceOrder.begin(), settings->serviceOrder.end(), id) == settings->serviceOrder.end()) {
					settings->serviceOrder.push_back(id);
					settings->serviceOn.push_back(id);
				}
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
	// the switches of the services are what the list says
	if (settings->serviceSet) {
		auto on = [settings](const char* id) { return std::find(settings->serviceOn.begin(), settings->serviceOn.end(), id) != settings->serviceOn.end(); };
		settings->emailServices = on("email");
		settings->github = on("github");
		settings->fediverse = on("mastodon");
		settings->bluesky = on("bluesky");
		settings->feeds = on("feeds");
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
	if (key == "services" || key == "mastodon_servers")
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
