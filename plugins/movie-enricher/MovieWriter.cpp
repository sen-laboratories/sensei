/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "MovieWriter.h"

#include <Node.h>
#include <TypeConstants.h>
#include <String.h>
#include <fs_attr.h>

#include <sen/Sen.h>
#include <sen/SenList.h>
#include <sen/SenOntoMovies.h>
#include <sen/SenSettingsPath.h>

#include "../../common/ThumbnailUtil.h"
#include "ListSeparator.h"

namespace movie {

namespace {

bool
HasAttr(BNode& node, const char* attribute)
{
	attr_info info;
	return node.GetAttrInfo(attribute, &info) == B_OK && info.size > 0;
}

void
PutString(BNode& node, const char* attribute, const std::string& value, bool overwrite)
{
	if (value.empty() || (!overwrite && HasAttr(node, attribute)))
		return;
	node.WriteAttr(attribute, B_STRING_TYPE, 0, value.c_str(), value.size() + 1);
}

void
PutList(BNode& node, const char* attribute, const std::vector<std::string>& values, bool overwrite)
{
	if (values.empty())
		return;
	PutString(node, attribute, sen::list::Join(values, ListSeparatorOf(sen::onto::movies::mime::kMovie, attribute)), overwrite);
}

}	// namespace


status_t
WriteMovie(const entry_ref& file, const Details& d, bool overwrite)
{
	BNode node(&file);
	status_t result = node.InitCheck();
	if (result != B_OK)
		return result;

	namespace attr = sen::onto::movies::attr;
	PutString(node, attr::kTitle, d.title, overwrite);
	if (d.originalTitle != d.title)
		PutString(node, attr::kOriginalTitle, d.originalTitle, overwrite);

	int64_t released = 0;
	if (!d.releaseDate.empty() && sen::settings::ParseTime(d.releaseDate, &released) && (overwrite || !HasAttr(node, attr::kReleaseDate))) {
		const time_t value = (time_t)released;
		node.WriteAttr(attr::kReleaseDate, B_TIME_TYPE, 0, &value, sizeof(value));
	}
	PutString(node, attr::kDuration, DurationIso(d.runtime), overwrite);
	PutString(node, attr::kDescription, d.overview, overwrite);
	PutList(node, attr::kGenre, d.genres, overwrite);
	PutList(node, attr::kInLanguage, d.languages, overwrite);
	PutList(node, attr::kCountryOfOrigin, d.countries, overwrite);
	PutString(node, attr::kImage, "https://www.themoviedb.org/movie/" + std::to_string(d.id), overwrite);
	PutString(node, attr::kPosterLicense, "The Movie Database (TMDB)", overwrite);

	// the links: the ones that the file has stay, the ones of TMDB are added (the link of TMDB is how the movie is found again)
	const char separator = ListSeparatorOf(sen::onto::movies::mime::kMovie, attr::kSameAs);
	std::vector<std::string> links;
	BString existing;
	if (node.ReadAttrString(attr::kSameAs, &existing) == B_OK)
		links = SplitValues(existing.String(), separator);
	for (const std::string& link : SameAs(d)) {
		bool known = false;
		for (const std::string& each : links)
			known = known || each == link;
		if (!known)
			links.push_back(link);
	}
	const std::string joined = sen::list::Join(links, separator);
	node.WriteAttr(attr::kSameAs, B_STRING_TYPE, 0, joined.c_str(), joined.size() + 1);

	// the semantic type: what a file is for SEN (Shoji shows its form for it); its type for Haiku stays
	if (!HasAttr(node, sen::attr::kType)) {
		const std::string type = sen::onto::movies::mime::kMovie;
		node.WriteAttr(sen::attr::kType, B_STRING_TYPE, 0, type.c_str(), type.size() + 1);
	}
	return B_OK;
}


status_t
WriteNames(const entry_ref& file, const std::vector<Credit>& credits, bool overwrite)
{
	BNode node(&file);
	status_t result = node.InitCheck();
	if (result != B_OK)
		return result;
	namespace attr = sen::onto::movies::attr;
	PutList(node, attr::kDirector, NamesWithRole(credits, "Director"), overwrite);
	PutList(node, attr::kActor, NamesWithRole(credits, "Actor"), overwrite);
	return B_OK;
}


status_t
WritePoster(const entry_ref& file, const std::string& image, bool overwrite)
{
	BNode node(&file);
	attr_info info;
	if (!overwrite && node.GetAttrInfo(THUMBNAIL_ATTR_NAME, &info) == B_OK && info.size > 0)
		return B_OK;
	return WriteThumbnail(&file, image);
}

}	// namespace movie
