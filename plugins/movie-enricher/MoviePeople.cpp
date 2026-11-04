/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "MoviePeople.h"

#include <Messenger.h>
#include <Node.h>
#include <TypeConstants.h>
#include <String.h>
#include <fs_attr.h>

#include <stdio.h>
#include <map>

#include <sen/Sen.h>
#include <sen/SenList.h>
#include <sen/SenOntoCore.h>
#include <sen/SenOntoMovies.h>
#include <sen/SenOntoPeople.h>
#include <sen/SenSettingsPath.h>

#include "../../common/EntityFile.h"
#include "../../common/ThumbnailUtil.h"

namespace movie {

namespace {

const char kListSeparator = ';';

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
WritePerson(const entry_ref& ref, const PersonDetails& person, const std::string& name, bool overwrite)
{
	BNode node(&ref);
	if (node.InitCheck() != B_OK)
		return;

	PutString(node, sen::onto::people::attr::kName, name, overwrite);
	PutString(node, sen::onto::people::attr::kDescription, person.biography, overwrite);

	int64_t birthday = 0;
	if (!person.birthday.empty() && sen::settings::ParseTime(person.birthday, &birthday) && (overwrite || !HasAttr(node, sen::onto::people::attr::kBirthDate))) {
		const time_t value = (time_t)birthday;
		node.WriteAttr(sen::onto::people::attr::kBirthDate, B_TIME_TYPE, 0, &value, sizeof(value));
	}

	// the links: TMDB, IMDb, Wikidata (other links that the file has stay: the list is added to)
	std::string links;
	BString existing;
	if (node.ReadAttrString(sen::onto::people::attr::kSameAs, &existing) == B_OK)
		links = existing.String();
	std::vector<std::string> values = sen::list::Split(links, kListSeparator);
	for (const std::string& link : SameAs(person)) {
		bool known = false;
		for (const std::string& value : values)
			known = known || value == link;
		if (!known)
			values.push_back(link);
	}
	const std::string joined = sen::list::Join(values, kListSeparator);
	node.WriteAttr(sen::onto::people::attr::kSameAs, B_STRING_TYPE, 0, joined.c_str(), joined.size() + 1);

	PutString(node, sen::onto::people::attr::kImage, "https://www.themoviedb.org/person/" + std::to_string(person.id), overwrite);
	PutString(node, sen::onto::people::attr::kPhotoCredit, "The Movie Database (TMDB)", overwrite);
}


/** the Contribution of a person to a movie, with its role, the character and the place in the credits */
status_t
Relate(const entry_ref& person, const entry_ref& movieFile, const Credit& credit)
{
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid())
		return B_NOT_INITIALIZED;	// the SEN server is not running: the people are there, the relations are made by the next enrichment

	BMessage properties;
	properties.AddString(sen::onto::core::attr::kRelRole, credit.role.c_str());
	if (!credit.character.empty())
		properties.AddString(sen::onto::core::attr::kRelCharacter, credit.character.c_str());
	if (credit.position > 0)
		properties.AddInt32(sen::onto::core::attr::kRelPosition, credit.position);

	BMessage add(sen::cmd::kRelationAdd), answer;
	add.AddString(sen::key::kRelationType, sen::onto::core::mime::kContribution);
	add.AddRef(sen::key::kSourceRef, &person);
	add.AddRef(sen::key::kTargetRef, &movieFile);
	add.AddMessage(sen::key::kRelationProperties, &properties);
	status_t result = server.SendMessage(&add, &answer, 10000000, 10000000);
	if (result != B_OK)
		return result;
	if (answer.GetInt32(sen::key::kStatus, 0) >= 400) {
		printf("the SEN server: %s\n", answer.GetString(sen::key::kDetail, "no relation"));
		return B_ERROR;
	}
	return B_OK;
}

}	// namespace


PeopleResult
MakePeople(const entry_ref& movieFile, const std::vector<Credit>& credits, PersonSource* source, bool overwrite, bool debug)
{
	PeopleResult result;
	std::map<int64_t, entry_ref> made;		// the people of this run: a person with several credits is fetched once
	std::map<int64_t, bool> failed;

	for (const Credit& credit : credits) {
		if (failed[credit.personId])
			continue;

		entry_ref person;
		auto known = made.find(credit.personId);
		if (known == made.end()) {
			PersonDetails details;
			if (!source->Fetch(credit.personId, &details)) {
				// (TMDB does not know the person any more, or the service is down: the credit has the name, which is enough for a person that has no more)
				details.id = credit.personId;
				details.name = credit.name;
				details.profilePath = credit.profilePath;
			}
			const std::string name = !details.name.empty() ? details.name : credit.name;
			if (details.profilePath.empty())
				details.profilePath = credit.profilePath;

			bool created = false;
			const std::string tmdbLink = "themoviedb.org/person/" + std::to_string(credit.personId);
			status_t status = sensei::FindOrCreateEntityFile(movieFile, sen::onto::people::mime::kPerson, name.c_str(), std::to_string(credit.personId).c_str(),
				[&tmdbLink](BNode& node) {
					// the person that is meant: the one with the link, or one of that name that has no link at all (a person made by hand)
					BString links;
					node.ReadAttrString(sen::onto::people::attr::kSameAs, &links);
					return links.IsEmpty() || links.FindFirst(tmdbLink.c_str()) >= 0 || links.FindFirst("themoviedb.org/person/") < 0;
				}, &person, &created);
			if (status != B_OK) {
				printf("could not make %s, skipping: %s\n", name.c_str(), strerror(status));
				failed[credit.personId] = true;
				result.skipped++;
				continue;
			}
			if (debug)
				printf("%s '%s'\n", created ? "making" : "updating", name.c_str());
			WritePerson(person, details, name, overwrite);

			// the photo, like the poster of the movie
			BNode node(&person);
			attr_info info;
			const bool hasPhoto = node.GetAttrInfo(THUMBNAIL_ATTR_NAME, &info) == B_OK && info.size > 0;
			if (!details.profilePath.empty() && (overwrite || !hasPhoto)) {
				std::string image;
				if (source->FetchPhoto(details.profilePath, &image) && !image.empty())
					WriteThumbnail(&person, image);
			}
			made[credit.personId] = person;
			result.people++;
			if (created)
				result.made++;
		} else {
			person = known->second;
		}

		status_t status = Relate(person, movieFile, credit);
		if (status == B_OK)
			result.contributions++;
		else if (status != B_NOT_INITIALIZED)
			printf("could not relate %s to the movie: %s\n", credit.name.c_str(), strerror(status));
	}
	return result;
}

}	// namespace movie
