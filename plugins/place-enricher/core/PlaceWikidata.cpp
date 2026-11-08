/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PlaceWikidata.h"

#include <cctype>
#include <cstdlib>

#include <nlohmann/json.hpp>

#include "PlaceNominatim.h"

namespace place {

namespace {

using Json = nlohmann::json;

/** `de-AT` is `de`: Wikidata knows the languages, not the countries (the variants fall back to them) */
std::string
BaseLanguage(const std::string& language)
{
	const size_t dash = language.find_first_of("-_");
	std::string base = dash == std::string::npos ? language : language.substr(0, dash);
	return base.empty() ? "en" : base;
}

/** the text of a language of a map of texts (`descriptions`), else of English */
std::string
Localized(const Json& map, const std::string& language)
{
	if (!map.is_object())
		return std::string();
	for (const std::string& code : {language, std::string("en")}) {
		auto it = map.find(code);
		if (it != map.end() && it->is_object()) {
			auto value = it->find("value");
			if (value != it->end() && value->is_string())
				return value->get<std::string>();
		}
	}
	return std::string();
}

/**
 * The file that a property names: the claim that is preferred, else the first that is not deprecated and has not ended (the qualifier "end time", P582), else the first that is not
 * deprecated.
 */
std::string
FileOf(const Json& claims, const char* property)
{
	auto it = claims.find(property);
	if (it == claims.end() || !it->is_array())
		return std::string();
	auto fileOf = [](const Json& claim) {
		auto snak = claim.find("mainsnak");
		if (snak == claim.end() || !snak->is_object())
			return std::string();
		auto data = snak->find("datavalue");
		if (data == snak->end() || !data->is_object())
			return std::string();
		auto value = data->find("value");
		return value != data->end() && value->is_string() ? value->get<std::string>() : std::string();
	};
	auto rank = [](const Json& claim) {
		auto r = claim.find("rank");
		return r != claim.end() && r->is_string() ? r->get<std::string>() : std::string("normal");
	};
	auto ended = [](const Json& claim) {
		auto qualifiers = claim.find("qualifiers");
		return qualifiers != claim.end() && qualifiers->is_object() && qualifiers->contains("P582");
	};
	for (const Json& claim : *it) {
		if (rank(claim) == "preferred" && !fileOf(claim).empty())
			return fileOf(claim);
	}
	for (const Json& claim : *it) {
		if (rank(claim) != "deprecated" && !ended(claim) && !fileOf(claim).empty())
			return fileOf(claim);
	}
	for (const Json& claim : *it) {
		if (rank(claim) != "deprecated" && !fileOf(claim).empty())
			return fileOf(claim);
	}
	return std::string();
}

std::string
Str(const Json& json, const char* key)
{
	auto it = json.find(key);
	return it != json.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/** the value of a field of the metadata of Commons (`{"Artist": {"value": "...", "source": "..."}}`) */
std::string
Meta(const Json& meta, const char* key)
{
	auto it = meta.find(key);
	if (it == meta.end() || !it->is_object())
		return std::string();
	return Str(*it, "value");
}

void
AppendUtf8(std::string* out, unsigned long code)
{
	if (code < 0x80) {
		*out += (char)code;
	} else if (code < 0x800) {
		*out += (char)(0xC0 | (code >> 6));
		*out += (char)(0x80 | (code & 0x3F));
	} else if (code < 0x10000) {
		*out += (char)(0xE0 | (code >> 12));
		*out += (char)(0x80 | ((code >> 6) & 0x3F));
		*out += (char)(0x80 | (code & 0x3F));
	} else {
		*out += (char)(0xF0 | (code >> 18));
		*out += (char)(0x80 | ((code >> 12) & 0x3F));
		*out += (char)(0x80 | ((code >> 6) & 0x3F));
		*out += (char)(0x80 | (code & 0x3F));
	}
}

}	// namespace


std::string
EntityUrl(const std::string& wikidataId, const std::string& language)
{
	const std::string base = BaseLanguage(language);
	std::string languages = base;
	if (base != "en")
		languages += "|en";
	return "https://www.wikidata.org/w/api.php?action=wbgetentities&ids=" + PercentEncode(wikidataId) + "&props=claims%7Cdescriptions%7Clabels&languages="
		+ PercentEncode(languages) + "&languagefallback=1&format=json";
}


std::string
ImageInfoUrl(const std::string& file, int width)
{
	return "https://commons.wikimedia.org/w/api.php?action=query&titles=" + PercentEncode("File:" + file)
		+ "&prop=imageinfo&iiprop=url%7Cextmetadata%7Cmime%7Csize&iiurlwidth=" + std::to_string(width)
		+ "&iiextmetadatafilter=LicenseShortName%7CUsageTerms%7CLicenseUrl%7CArtist%7CCredit%7CAttributionRequired&format=json";
}


bool
ParseEntity(const std::string& text, const std::string& id, const std::string& language, Entity* entity)
{
	Json json = Json::parse(text, nullptr, false);
	if (!json.is_object())
		return false;
	auto entities = json.find("entities");
	if (entities == json.end() || !entities->is_object())
		return false;
	auto item = entities->find(id);
	if (item == entities->end() || !item->is_object() || item->contains("missing"))
		return false;

	const std::string base = BaseLanguage(language);
	Entity e;
	e.id = id;
	auto labels = item->find("labels");
	if (labels != item->end())
		e.label = Localized(*labels, base);
	auto descriptions = item->find("descriptions");
	if (descriptions != item->end())
		e.description = Localized(*descriptions, base);
	auto claims = item->find("claims");
	if (claims != item->end() && claims->is_object()) {
		e.photo = FileOf(*claims, "P18");
		e.coatOfArms = FileOf(*claims, "P94");
		e.flag = FileOf(*claims, "P41");
		e.locatorMap = FileOf(*claims, "P242");
		e.seal = FileOf(*claims, "P158");
		e.logo = FileOf(*claims, "P154");
	}
	*entity = e;
	return true;
}


bool
ParseImageInfo(const std::string& text, Image* image)
{
	Json json = Json::parse(text, nullptr, false);
	if (!json.is_object())
		return false;
	auto query = json.find("query");
	if (query == json.end() || !query->is_object())
		return false;
	auto pages = query->find("pages");
	if (pages == query->end() || !pages->is_object() || pages->empty())
		return false;
	auto first = pages->begin();		// (the iterator is kept: the page is a value of the answer, not of a temporary)
	const Json& page = first.value();
	if (!page.is_object() || page.contains("missing"))
		return false;
	auto infos = page.find("imageinfo");
	if (infos == page.end() || !infos->is_array() || infos->empty() || !(*infos)[0].is_object())
		return false;
	const Json& info = (*infos)[0];

	Image i;
	i.thumbnailUrl = Str(info, "thumburl");
	if (i.thumbnailUrl.empty())
		i.thumbnailUrl = Str(info, "url");
	i.pageUrl = Str(info, "descriptionurl");
	i.mime = Str(info, "mime");
	auto meta = info.find("extmetadata");
	if (meta != info.end() && meta->is_object()) {
		i.license = StripHtml(Meta(*meta, "LicenseShortName"));
		if (i.license.empty())
			i.license = StripHtml(Meta(*meta, "UsageTerms"));
		i.licenseUrl = Meta(*meta, "LicenseUrl");
		i.artist = StripHtml(Meta(*meta, "Artist"));
		i.credit = StripHtml(Meta(*meta, "Credit"));
		i.attributionRequired = Meta(*meta, "AttributionRequired") == "true";
	}
	const std::string title = Str(page, "title");
	if (title.compare(0, 5, "File:") == 0)
		i.file = title.substr(5);
	*image = i;
	return !i.thumbnailUrl.empty();
}


Image
PickImage(const Entity& entity, bool administrative)
{
	struct Choice {
		const std::string*	file;
		const char*			kind;
	};
	const std::vector<Choice> area = {
		{&entity.coatOfArms, "coat of arms"}, {&entity.flag, "flag"}, {&entity.locatorMap, "locator map"}, {&entity.seal, "seal"}, {&entity.photo, "photo"},
	};
	const std::vector<Choice> other = {
		{&entity.photo, "photo"}, {&entity.logo, "logo"},
	};
	for (const Choice& choice : administrative ? area : other) {
		if (!choice.file->empty()) {
			Image image;
			image.file = *choice.file;
			image.kind = choice.kind;
			return image;
		}
	}
	return Image();
}


std::string
CreditOf(const Image& image)
{
	std::string credit = image.artist;
	if (credit.empty() && image.attributionRequired)
		credit = image.credit;
	std::string text = credit;
	if (!image.license.empty())
		text += (text.empty() ? "" : ", ") + image.license;
	if (text.empty())
		return std::string();
	return text + " (Wikimedia Commons)";
}


std::string
StripHtml(const std::string& html)
{
	std::string text;
	bool inTag = false;
	for (size_t i = 0; i < html.size(); i++) {
		const char c = html[i];
		if (inTag) {
			if (c == '>')
				inTag = false;
		} else if (c == '<') {
			inTag = true;
		} else if (c == '&') {
			const size_t end = html.find(';', i);
			if (end != std::string::npos && end - i <= 10) {
				const std::string entity = html.substr(i + 1, end - i - 1);
				std::string decoded;
				if (entity == "amp") decoded = "&";
				else if (entity == "lt") decoded = "<";
				else if (entity == "gt") decoded = ">";
				else if (entity == "quot") decoded = "\"";
				else if (entity == "apos") decoded = "'";
				else if (entity == "nbsp") decoded = " ";
				else if (entity.size() > 1 && entity[0] == '#') {
					const bool hex = entity[1] == 'x' || entity[1] == 'X';
					const unsigned long code = std::strtoul(entity.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
					if (code > 0)
						AppendUtf8(&decoded, code);
				}
				if (!decoded.empty()) {
					text += decoded;
					i = end;
					continue;
				}
			}
			text += c;
		} else {
			text += c;
		}
	}
	// blanks: one between words, none at the ends
	std::string out;
	bool space = false;
	for (unsigned char c : text) {
		if (std::isspace(c)) {
			space = !out.empty();
		} else {
			if (space)
				out += ' ';
			space = false;
			out += (char)c;
		}
	}
	return out;
}

}	// namespace place
