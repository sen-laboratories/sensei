/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "Hunt.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <set>

#include <nlohmann/json.hpp>

#include "FeedText.h"

namespace robin {

namespace {

using Json = nlohmann::json;

const char* const kAcceptJson = "application/json";
const long kHuntCacheSeconds = 24L * 3600;		// (an answer of a search is kept a day: the same person is looked at again and again while the file is edited)
const size_t kMaxDetails = 5;					// (GitHub allows 60 requests an hour without a key: the details are asked for the best few)
const size_t kMaxHits = 25;

Json
Parse(const std::string& text)
{
	try {
		return Json::parse(text);
	} catch (...) {
		return Json();
	}
}

std::string
Text(const Json& object, const char* key)
{
	if (!object.is_object() || !object.contains(key) || !object[key].is_string())
		return std::string();
	return object[key].get<std::string>();
}

int64_t
Number(const Json& object, const char* key)
{
	if (!object.is_object() || !object.contains(key) || !object[key].is_number())
		return 0;
	return object[key].get<int64_t>();
}

std::string
Lower(std::string text)
{
	for (char& c : text)
		c = (char)std::tolower((unsigned char)c);
	return text;
}

// letters without their accents (the Latin ones that names have), lower case; everything that is no letter or digit is a blank
std::string
Fold(const std::string& text)
{
	struct Pair { const char* from; const char* to; };
	static const Pair pairs[] = {
		{"\xC3\xA1", "a"}, {"\xC3\xA0", "a"}, {"\xC3\xA2", "a"}, {"\xC3\xA4", "a"}, {"\xC3\xA3", "a"}, {"\xC3\xA5", "a"}, {"\xC3\x81", "a"}, {"\xC3\x84", "a"}, {"\xC3\x80", "a"},
		{"\xC3\xA9", "e"}, {"\xC3\xA8", "e"}, {"\xC3\xAA", "e"}, {"\xC3\xAB", "e"}, {"\xC3\x89", "e"},
		{"\xC3\xAD", "i"}, {"\xC3\xAC", "i"}, {"\xC3\xAE", "i"}, {"\xC3\xAF", "i"},
		{"\xC3\xB3", "o"}, {"\xC3\xB2", "o"}, {"\xC3\xB4", "o"}, {"\xC3\xB6", "o"}, {"\xC3\xB5", "o"}, {"\xC3\xB8", "o"}, {"\xC3\x96", "o"},
		{"\xC3\xBA", "u"}, {"\xC3\xB9", "u"}, {"\xC3\xBB", "u"}, {"\xC3\xBC", "u"}, {"\xC3\x9C", "u"},
		{"\xC3\xB1", "n"}, {"\xC3\xA7", "c"}, {"\xC3\x9F", "ss"}, {"\xC5\xA1", "s"}, {"\xC5\x82", "l"}, {"\xC5\x99", "r"}, {"\xC4\x8D", "c"}, {"\xC5\xBE", "z"},
	};
	std::string out;
	for (size_t i = 0; i < text.size();) {
		bool replaced = false;
		if ((unsigned char)text[i] >= 0xC3 && i + 1 < text.size()) {
			for (const Pair& pair : pairs) {
				const size_t length = strlen(pair.from);
				if (text.compare(i, length, pair.from) == 0) {
					out += pair.to;
					i += length;
					replaced = true;
					break;
				}
			}
		}
		if (replaced)
			continue;
		const unsigned char c = (unsigned char)text[i++];
		out.push_back(std::isalnum(c) ? (char)std::tolower(c) : ' ');
	}
	return out;
}

std::vector<std::string>
Words(const std::string& text)
{
	std::vector<std::string> words;
	const std::string folded = Fold(text);
	size_t i = 0;
	while (i < folded.size()) {
		while (i < folded.size() && folded[i] == ' ')
			i++;
		size_t start = i;
		while (i < folded.size() && folded[i] != ' ')
			i++;
		if (i > start)
			words.push_back(folded.substr(start, i - start));
	}
	return words;
}

// an address without its scheme, `www.`, query and the slash at its end, in lower case: two addresses of the same page compare as equal
std::string
Comparable(std::string url)
{
	url = Lower(url);
	const size_t scheme = url.find("://");
	if (scheme != std::string::npos)
		url = url.substr(scheme + 3);
	if (url.compare(0, 4, "www.") == 0)
		url = url.substr(4);
	url = url.substr(0, url.find_first_of("?#"));
	while (!url.empty() && url.back() == '/')
		url.pop_back();
	return url;
}

bool
Contains(const std::string& haystack, const std::string& needle)
{
	if (needle.empty())
		return false;
	return Fold(haystack).find(Fold(needle)) != std::string::npos;
}

std::string
WithScheme(const std::string& address)
{
	if (address.empty() || address.find("://") != std::string::npos)
		return address;
	return "https://" + address;
}

void
AddLink(HuntHit* hit, const std::string& link)
{
	if (link.empty() || link == hit->url || std::find(hit->links.begin(), hit->links.end(), link) != hit->links.end())
		return;
	hit->links.push_back(link);
}

}	// namespace


Hunter::Hunter(PersonEnricher::HttpGetFunc httpGet, const Settings& settings)
	:
	fHttpGet(std::move(httpGet)),
	fSettings(settings)
{
}


// ---- handles, names, scores

std::string
Hunter::UrlEncode(const std::string& text)
{
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : text) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out.push_back((char)c);
		} else {
			out.push_back('%');
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 15]);
		}
	}
	return out;
}


Handle
Hunter::ParseHandle(const std::string& text)
{
	Handle handle;
	size_t start = 0, end = text.size();
	while (start < end && std::isspace((unsigned char)text[start]))
		start++;
	while (end > start && (std::isspace((unsigned char)text[end - 1]) || text[end - 1] == '.' || text[end - 1] == ','))
		end--;
	std::string value = text.substr(start, end - start);
	if (value.compare(0, 5, "acct:") == 0)
		value = value.substr(5);
	while (!value.empty() && value[0] == '@')
		value.erase(0, 1);
	if (value.empty() || value.find_first_of(" \t/:#?") != std::string::npos)
		return handle;

	const size_t at = value.find('@');
	if (at != std::string::npos) {
		if (at == 0 || at + 1 >= value.size() || value.find('@', at + 1) != std::string::npos || value.find('.', at) == std::string::npos)
			return handle;
		handle.kind = Handle::kFediverse;
		handle.user = value.substr(0, at);
		handle.host = Lower(value.substr(at + 1));
		handle.text = "@" + handle.user + "@" + handle.host;
		return handle;
	}
	for (char c : value) {
		if (!(std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_'))
			return handle;
	}
	const size_t dot = value.find('.');
	if (dot != std::string::npos && dot > 0 && dot + 1 < value.size() && value.find("..") == std::string::npos) {
		handle.kind = Handle::kDomain;			// (a Bluesky handle is a domain: `alice.bsky.social`, `sen-labs.org`)
		handle.user = handle.text = Lower(value);
		return handle;
	}
	handle.kind = Handle::kPlain;
	handle.user = handle.text = value;
	return handle;
}


float
Hunter::NameSimilarity(const std::string& a, const std::string& b)
{
	const std::vector<std::string> x = Words(a), y = Words(b);
	if (x.empty() || y.empty())
		return 0;
	std::set<std::string> left(x.begin(), x.end()), right(y.begin(), y.end());
	size_t same = 0;
	for (const std::string& word : left) {
		if (right.count(word) != 0)
			same++;
	}
	if (same == 0)
		return 0;
	const float small = (float)std::min(left.size(), right.size()), large = (float)std::max(left.size(), right.size());
	return std::min(1.0f, ((float)same / small) * (0.7f + 0.3f * (float)same / large));
}


float
Hunter::Score(HuntHit* hit, const PersonProfile& wanted)
{
	float nameSimilarity = 0;
	std::vector<std::string> names = {wanted.name};
	if (!wanted.givenName.empty() || !wanted.familyName.empty())
		names.push_back(wanted.givenName + " " + wanted.familyName);
	for (const std::string& name : names) {
		if (!name.empty() && hit->name.size() > 0)
			nameSimilarity = std::max(nameSimilarity, NameSimilarity(name, hit->name));
	}

	float score = 0.7f * nameSimilarity;

	// the handle that the file has is the account: an exact one (also its server, or its domain) is as good as a link
	const Handle handle = ParseHandle(wanted.nick);
	if (handle.kind != Handle::kNone) {
		const std::string id = Lower(hit->id);
		const bool same = handle.kind == Handle::kFediverse ? id == Lower(handle.user + "@" + handle.host) : id == Lower(handle.user);
		if (same)
			score = std::max(score, 0.85f + 0.1f * nameSimilarity);
	}

	// a page that the file links to is the hit
	std::set<std::string> known;
	for (const std::string& link : PersonEnricher::Links(wanted))
		known.insert(Comparable(link));
	if (!known.empty()) {
		bool linked = known.count(Comparable(hit->url)) != 0;
		for (const std::string& link : hit->links)
			linked = linked || known.count(Comparable(link)) != 0;
		if (linked)
			score = std::max(score, 0.9f);
	}

	// what else the file says (the place, the company, the job) fits what the hit says about itself
	const std::string about = hit->description + " " + hit->location + " " + hit->company + " " + hit->jobTitle;
	for (const std::string* filter : {&wanted.location, &wanted.company, &wanted.jobTitle}) {
		if (score > 0 && Contains(about, *filter))
			score += 0.06f;
	}
	// (a hit that is well known and complete is a little more likely to be the one)
	if (score > 0) {
		if (hit->followers > 0)
			score += std::min(0.05f, (float)std::log10((double)hit->followers) / 100.0f);
		if (!hit->avatarUrl.empty())
			score += 0.02f;
		if (!hit->description.empty())
			score += 0.03f;
	}
	hit->score = std::min(1.0f, score);
	return hit->score;
}


bool
Hunter::IsClearWinner(const std::vector<HuntHit>& hits)
{
	if (hits.empty())
		return false;
	const float best = hits[0].score;
	const float next = hits.size() > 1 ? hits[1].score : 0;
	return best >= 0.8f && best - next >= 0.25f;
}


// ---- the answers of the services

bool
Hunter::ParseBlueskyProfile(const std::string& json, HuntHit* hit)
{
	const Json j = Parse(json);
	const std::string handle = Text(j, "handle");
	if (!j.is_object() || handle.empty())
		return false;
	hit->service = "bluesky";
	hit->id = handle;
	hit->name = Text(j, "displayName").empty() ? handle : Text(j, "displayName");
	hit->url = "https://bsky.app/profile/" + handle;
	hit->description = Text(j, "description");
	hit->avatarUrl = Text(j, "avatar");
	hit->followers = Number(j, "followersCount");
	return true;
}


bool
Hunter::ParseBlueskySearch(const std::string& json, std::vector<HuntHit>* hits)
{
	const Json j = Parse(json);
	if (!j.is_object() || !j.contains("actors") || !j["actors"].is_array())
		return false;
	for (const Json& actor : j["actors"]) {
		HuntHit hit;
		if (ParseBlueskyProfile(actor.dump(), &hit))
			hits->push_back(hit);
	}
	return true;
}


bool
Hunter::ParseMastodonAccount(const std::string& json, const std::string& host, HuntHit* hit)
{
	const Json j = Parse(json);
	if (!j.is_object() || Text(j, "username").empty())
		return false;
	const std::string acct = Text(j, "acct");
	hit->service = "mastodon";
	hit->id = acct.find('@') == std::string::npos ? Text(j, "username") + "@" + host : acct;
	// (an account on a bridge, Bridgy Fed, is a copy of one on Bluesky or the web: the original is the answer, not its mirror)
	const std::string where = hit->id.substr(hit->id.find('@') + 1);
	if (where == "brid.gy" || (where.size() > 8 && where.compare(where.size() - 8, 8, ".brid.gy") == 0))
		return false;
	hit->name = Text(j, "display_name").empty() ? Text(j, "username") : Text(j, "display_name");
	hit->url = Text(j, "url");
	hit->description = feedtext::HtmlToPlainText(Text(j, "note"));
	hit->avatarUrl = Text(j, "avatar");
	hit->followers = Number(j, "followers_count");
	// (the profile fields: a person puts the websites and the other accounts there; a link of the field is a page of the same person)
	if (j.contains("fields") && j["fields"].is_array()) {
		for (const Json& field : j["fields"]) {
			const std::string value = Text(field, "value");
			const size_t href = value.find("href=\"");
			if (href != std::string::npos) {
				const size_t end = value.find('"', href + 6);
				if (end != std::string::npos)
					AddLink(hit, value.substr(href + 6, end - href - 6));
			}
		}
	}
	return !hit->url.empty();
}


bool
Hunter::ParseMastodonSearch(const std::string& json, const std::string& host, std::vector<HuntHit>* hits)
{
	const Json j = Parse(json);
	if (!j.is_object() || !j.contains("accounts") || !j["accounts"].is_array())
		return false;
	for (const Json& account : j["accounts"]) {
		HuntHit hit;
		if (ParseMastodonAccount(account.dump(), host, &hit))
			hits->push_back(hit);
	}
	return true;
}


bool
Hunter::ParseGitHubSearch(const std::string& json, std::vector<HuntHit>* hits)
{
	const Json j = Parse(json);
	if (!j.is_object() || !j.contains("items") || !j["items"].is_array())
		return false;
	for (const Json& item : j["items"]) {
		if (Text(item, "type") == "Organization" || Text(item, "login").empty())
			continue;
		HuntHit hit;
		hit.service = "github";
		hit.id = Text(item, "login");
		hit.name = hit.id;
		hit.url = Text(item, "html_url");
		hit.avatarUrl = Text(item, "avatar_url");
		hits->push_back(hit);
	}
	return true;
}


bool
Hunter::ParseGitHubUser(const std::string& json, HuntHit* hit)
{
	const Json j = Parse(json);
	if (!j.is_object() || Text(j, "login").empty() || Text(j, "type") == "Organization")
		return false;
	hit->service = "github";
	hit->id = Text(j, "login");
	hit->name = Text(j, "name").empty() ? hit->id : Text(j, "name");
	hit->url = Text(j, "html_url");
	hit->description = Text(j, "bio");
	hit->location = Text(j, "location");
	hit->company = Text(j, "company");
	hit->avatarUrl = Text(j, "avatar_url");
	hit->homepage = WithScheme(Text(j, "blog"));
	AddLink(hit, hit->homepage);
	if (!Text(j, "twitter_username").empty())
		AddLink(hit, "https://x.com/" + Text(j, "twitter_username"));
	hit->followers = Number(j, "followers");
	return !hit->url.empty();
}


bool
Hunter::ParseWikidataSearch(const std::string& json, std::vector<std::string>* ids)
{
	const Json j = Parse(json);
	if (!j.is_object() || !j.contains("search") || !j["search"].is_array())
		return false;
	for (const Json& entry : j["search"]) {
		if (!Text(entry, "id").empty())
			ids->push_back(Text(entry, "id"));
	}
	return true;
}


namespace {

// the values of a property of a Wikidata entry (strings, or the id of an entity)
std::vector<std::string>
Claims(const Json& entity, const char* property)
{
	std::vector<std::string> values;
	if (!entity.contains("claims") || !entity["claims"].is_object() || !entity["claims"].contains(property) || !entity["claims"][property].is_array())
		return values;
	for (const Json& claim : entity["claims"][property]) {
		if (!claim.contains("mainsnak") || !claim["mainsnak"].contains("datavalue") || !claim["mainsnak"]["datavalue"].contains("value"))
			continue;
		const Json& value = claim["mainsnak"]["datavalue"]["value"];
		if (value.is_string())
			values.push_back(value.get<std::string>());
		else if (value.is_object() && value.contains("id") && value["id"].is_string())
			values.push_back(value["id"].get<std::string>());
	}
	return values;
}

}	// namespace


bool
Hunter::ParseWikidataEntities(const std::string& json, std::vector<HuntHit>* hits)
{
	const Json j = Parse(json);
	if (!j.is_object() || !j.contains("entities") || !j["entities"].is_object())
		return false;
	for (auto it = j["entities"].begin(); it != j["entities"].end(); ++it) {
		const Json& entity = it.value();
		// (a person only: a human, Q5)
		const std::vector<std::string> kinds = Claims(entity, "P31");
		if (std::find(kinds.begin(), kinds.end(), "Q5") == kinds.end())
			continue;
		HuntHit hit;
		hit.service = "wikidata";
		hit.id = hit.wikidata = it.key();
		hit.url = "https://www.wikidata.org/wiki/" + hit.id;
		if (entity.contains("labels") && entity["labels"].contains("en"))
			hit.name = Text(entity["labels"]["en"], "value");
		if (entity.contains("descriptions") && entity["descriptions"].contains("en"))
			hit.description = Text(entity["descriptions"]["en"], "value");
		if (hit.name.empty())
			continue;
		const std::vector<std::string> images = Claims(entity, "P18");
		if (!images.empty()) {
			std::string file = images[0];
			std::replace(file.begin(), file.end(), ' ', '_');
			hit.avatarUrl = "https://commons.wikimedia.org/wiki/Special:FilePath/" + UrlEncode(file) + "?width=512";
		}
		const std::vector<std::string> orcid = Claims(entity, "P496");
		if (!orcid.empty()) {
			hit.orcid = orcid[0];
			AddLink(&hit, "https://orcid.org/" + orcid[0]);
		}
		for (const std::string& user : Claims(entity, "P2037"))
			AddLink(&hit, "https://github.com/" + user);
		for (const std::string& site : Claims(entity, "P856")) {
			if (hit.homepage.empty())
				hit.homepage = site;
			AddLink(&hit, site);
		}
		for (const std::string& address : Claims(entity, "P4033")) {		// (`user@host`, sometimes with the `@` in front)
			const Handle handle = ParseHandle(address);
			if (handle.kind == Handle::kFediverse)
				AddLink(&hit, "https://" + handle.host + "/@" + handle.user);
		}
		for (const std::string& user : Claims(entity, "P2002"))
			AddLink(&hit, "https://x.com/" + user);
		for (const std::string& user : Claims(entity, "P2003"))
			AddLink(&hit, "https://www.instagram.com/" + user);
		for (const std::string& user : Claims(entity, "P6634"))
			AddLink(&hit, "https://www.linkedin.com/in/" + user);
		hits->push_back(hit);
	}
	return true;
}


bool
Hunter::ParseOrcidSearch(const std::string& json, std::vector<std::string>* ids)
{
	const Json j = Parse(json);
	if (!j.is_object() || !j.contains("result") || !j["result"].is_array())
		return false;
	for (const Json& entry : j["result"]) {
		if (entry.contains("orcid-identifier") && !Text(entry["orcid-identifier"], "path").empty())
			ids->push_back(Text(entry["orcid-identifier"], "path"));
	}
	return true;
}


bool
Hunter::ParseOrcidPerson(const std::string& json, const std::string& id, HuntHit* hit)
{
	const Json j = Parse(json);
	if (!j.is_object() || !j.contains("name") || !j["name"].is_object())
		return false;
	auto value = [](const Json& object, const char* key) {
		return object.contains(key) && object[key].is_object() ? Text(object[key], "value") : std::string();
	};
	const std::string given = value(j["name"], "given-names"), family = value(j["name"], "family-name");
	hit->service = "orcid";
	hit->id = hit->orcid = id;
	hit->name = given + (given.empty() || family.empty() ? "" : " ") + family;
	hit->url = "https://orcid.org/" + id;
	if (j.contains("biography") && j["biography"].is_object())
		hit->description = Text(j["biography"], "content");
	if (j.contains("researcher-urls") && j["researcher-urls"].contains("researcher-url") && j["researcher-urls"]["researcher-url"].is_array()) {
		for (const Json& entry : j["researcher-urls"]["researcher-url"]) {
			if (entry.contains("url") && entry["url"].is_object()) {
				const std::string link = Text(entry["url"], "value");
				if (hit->homepage.empty())
					hit->homepage = link;
				AddLink(hit, link);
			}
		}
	}
	return !hit->name.empty();
}


// ---- the search

bool
Hunter::_Get(const std::string& url, const char* accept, std::string* body)
{
	int status = 0;
	body->clear();
	return fHttpGet(url, accept, kHuntCacheSeconds, body, &status) && !body->empty() && (status == 0 || (status >= 200 && status < 300));
}


namespace {

void
AddUnique(std::vector<HuntHit>* hits, const HuntHit& hit)
{
	for (const HuntHit& known : *hits) {
		if (known.service == hit.service && Lower(known.id) == Lower(hit.id))
			return;
	}
	hits->push_back(hit);
}

}	// namespace


void
Hunter::_SearchBluesky(const PersonProfile& wanted, std::vector<HuntHit>* hits)
{
	std::string body;
	// the handle: a domain is a handle of Bluesky if there is an account of that name (`sen-labs.org` is one); a plain name is one on `bsky.social`
	const Handle handle = ParseHandle(wanted.nick);
	std::string actor;
	if (handle.kind == Handle::kDomain)
		actor = handle.text;
	else if (handle.kind == Handle::kPlain)
		actor = handle.text + ".bsky.social";
	if (!actor.empty() && _Get("https://public.api.bsky.app/xrpc/app.bsky.actor.getProfile?actor=" + UrlEncode(actor), kAcceptJson, &body)) {
		HuntHit hit;
		if (ParseBlueskyProfile(body, &hit))
			AddUnique(hits, hit);
	}
	if (!wanted.name.empty() && _Get("https://public.api.bsky.app/xrpc/app.bsky.actor.searchActors?limit=10&q=" + UrlEncode(wanted.name), kAcceptJson, &body)) {
		std::vector<HuntHit> found;
		ParseBlueskySearch(body, &found);
		for (const HuntHit& hit : found)
			AddUnique(hits, hit);
	}
}


void
Hunter::_SearchMastodon(const PersonProfile& wanted, std::vector<HuntHit>* hits)
{
	std::string body;
	const Handle handle = ParseHandle(wanted.nick);
	if (handle.kind == Handle::kFediverse && _Get("https://" + handle.host + "/api/v1/accounts/lookup?acct=" + UrlEncode(handle.user), kAcceptJson, &body)) {
		HuntHit hit;
		if (ParseMastodonAccount(body, handle.host, &hit))
			AddUnique(hits, hit);
	}
	if (wanted.name.empty())
		return;
	for (const std::string& server : fSettings.mastodonServers) {
		if (_Get("https://" + server + "/api/v2/search?type=accounts&limit=10&q=" + UrlEncode(wanted.name), kAcceptJson, &body)) {
			std::vector<HuntHit> found;
			ParseMastodonSearch(body, server, &found);
			for (const HuntHit& hit : found)
				AddUnique(hits, hit);
		}
	}
}


void
Hunter::_SearchGitHub(const PersonProfile& wanted, std::vector<HuntHit>* hits)
{
	std::string body;
	const Handle handle = ParseHandle(wanted.nick);
	if (handle.kind == Handle::kPlain && _Get("https://api.github.com/users/" + UrlEncode(handle.text), kAcceptJson, &body)) {
		HuntHit hit;
		if (ParseGitHubUser(body, &hit))
			AddUnique(hits, hit);
	}
	if (wanted.name.empty() || !_Get("https://api.github.com/search/users?per_page=10&q=" + UrlEncode(wanted.name + " in:name type:user"), kAcceptJson, &body))
		return;
	std::vector<HuntHit> found;
	if (!ParseGitHubSearch(body, &found))
		return;
	// (the answer of a search has only the login: the name, the bio and the place are asked for the first few)
	size_t details = 0;
	for (HuntHit& hit : found) {
		HuntHit full;
		if (details++ < kMaxDetails && _Get("https://api.github.com/users/" + UrlEncode(hit.id), kAcceptJson, &body) && ParseGitHubUser(body, &full))
			hit = full;
		AddUnique(hits, hit);
	}
}


void
Hunter::_SearchWikidata(const PersonProfile& wanted, std::vector<HuntHit>* hits)
{
	if (wanted.name.empty())
		return;
	std::string body;
	if (!_Get("https://www.wikidata.org/w/api.php?action=wbsearchentities&type=item&format=json&limit=8&language=en&search=" + UrlEncode(wanted.name), kAcceptJson, &body))
		return;
	std::vector<std::string> ids;
	if (!ParseWikidataSearch(body, &ids) || ids.empty())
		return;
	std::string joined;
	for (const std::string& id : ids)
		joined += (joined.empty() ? "" : "|") + id;
	if (!_Get("https://www.wikidata.org/w/api.php?action=wbgetentities&format=json&props=labels|descriptions|claims&languages=en&ids=" + UrlEncode(joined), kAcceptJson, &body))
		return;
	std::vector<HuntHit> found;
	ParseWikidataEntities(body, &found);
	for (const HuntHit& hit : found)
		AddUnique(hits, hit);
}


void
Hunter::_SearchOrcid(const PersonProfile& wanted, std::vector<HuntHit>* hits)
{
	std::string given = wanted.givenName, family = wanted.familyName;
	if (given.empty() || family.empty()) {
		const std::vector<std::string> words = Words(wanted.name);
		if (words.size() < 2)
			return;
		given = words.front();
		family = words.back();
	}
	std::string body;
	if (!_Get("https://pub.orcid.org/v3.0/search/?rows=5&q=" + UrlEncode("given-names:" + given + " AND family-name:" + family), kAcceptJson, &body))
		return;
	std::vector<std::string> ids;
	if (!ParseOrcidSearch(body, &ids))
		return;
	for (const std::string& id : ids) {
		HuntHit hit;
		if (_Get("https://pub.orcid.org/v3.0/" + id + "/person", kAcceptJson, &body) && ParseOrcidPerson(body, id, &hit))
			AddUnique(hits, hit);
	}
}


std::vector<HuntHit>
Hunter::Search(const PersonProfile& wanted)
{
	return Search(wanted, fSettings.HuntServices());
}


std::vector<HuntHit>
Hunter::Search(const PersonProfile& wanted, const std::vector<std::string>& services)
{
	std::vector<HuntHit> hits;
	if (wanted.name.empty() && ParseHandle(wanted.nick).kind == Handle::kNone)
		return hits;

	for (const std::string& service : services) {
		if (!fSettings.Uses(service))
			continue;
		if (service == "bluesky")
			_SearchBluesky(wanted, &hits);
		else if (service == "mastodon")
			_SearchMastodon(wanted, &hits);
		else if (service == "github")
			_SearchGitHub(wanted, &hits);
		else if (service == "wikidata")
			_SearchWikidata(wanted, &hits);
		else if (service == "orcid")
			_SearchOrcid(wanted, &hits);
	}

	for (HuntHit& hit : hits)
		Score(&hit, wanted);
	// (the best first; the order of the services settles a tie)
	std::stable_sort(hits.begin(), hits.end(), [](const HuntHit& a, const HuntHit& b) { return a.score > b.score; });
	// (a hit that does not fit at all is no hit)
	hits.erase(std::remove_if(hits.begin(), hits.end(), [](const HuntHit& hit) { return hit.score < 0.3f; }), hits.end());
	if (hits.size() > kMaxHits)
		hits.resize(kMaxHits);
	return hits;
}

}	// namespace robin
