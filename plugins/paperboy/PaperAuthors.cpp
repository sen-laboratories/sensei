/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "../../common/EntityFile.h"
#include "PaperAuthors.h"

#include <Directory.h>
#include <File.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Query.h>
#include <String.h>
#include <Volume.h>
#include <fs_attr.h>
#include <fs_index.h>

#include <algorithm>

#include <sen/Sen.h>
#include <sen/SenList.h>
#include <sen/SenMimeTypes.h>
#include <sen/SenOntoBooks.h>
#include <sen/SenOntoPeople.h>
#include <spdlog/spdlog.h>

#include "PaperDate.h"
#include "PaperHttp.h"
#include "PaperPeople.h"
#include "../../common/ThumbnailUtil.h"

using namespace paperboy;

namespace {

/** the sources are small and polite: ORCID allows many requests a second, Wikimedia asks for requests one after the other */
constexpr bigtime_t kPause = 300 * 1000LL;

HttpOptions
Json()
{
	HttpOptions options;
	options.minInterval = kPause;
	options.maxAttempts = 1;
	return options;
}

HttpOptions
Image()
{
	HttpOptions options = Json();
	options.accept = "image/jpeg,image/png,image/*";
	return options;
}

bool
HasAttr(BNode& node, const char* name)
{
	attr_info info;
	return node.GetAttrInfo(name, &info) == B_OK;
}

}	// namespace


AuthorMaker::AuthorMaker(HttpClient* http, const Settings& settings, bool overwrite)
	:
	fHttp(http),
	fSettings(settings),
	fOverwrite(overwrite)
{
}


int32
AuthorMaker::MakeAuthors(const entry_ref& file, const Paper& paper)
{
	int32 made = 0;
	int32 looked = 0;
	for (const Author& author : paper.authors) {
		if (looked >= fSettings.maxAuthors)
			break;
		if (author.name.empty() || (fSettings.authors == "orcid" && author.orcid.empty()))
			continue;
		looked++;

		PersonData person;
		person.name = author.name;
		person.orcid = author.orcid;
		std::string photo;
		if (!author.orcid.empty())
			_Lookup(&person, &photo);
		// (a name that the paper gives is the one used by the paper: ORCID's own spelling is kept when there is one)
		if (person.name.empty())
			person.name = author.name;

		entry_ref ref;
		bool created = false;
		status_t result = _FindOrCreate(file, person, &ref, &created);
		if (result != B_OK) {
			spdlog::warn("could not make the author {}: {}", person.name, strerror(result));
			continue;
		}
		spdlog::info("{} author '{}'{}", created ? "making" : "updating", person.name, person.orcid.empty() ? "" : " (" + person.orcid + ")");

		_WriteAttributes(ref, person);
		if (!photo.empty()) {
			BNode node(&ref);
			if (fOverwrite || !HasAttr(node, THUMBNAIL_ATTR_NAME)) {
				result = WriteThumbnail(&ref, photo);
				if (result != B_OK)
					spdlog::warn("could not write the photo of {}: {}", person.name, strerror(result));
			}
		}

		result = _Relate(ref, file);
		if (result != B_OK) {
			spdlog::warn("could not relate {} to the paper: {}", person.name, strerror(result));
			continue;
		}
		made++;
	}
	return made;
}


bool
AuthorMaker::_Lookup(PersonData* person, std::string* photo)
{
	bool known = false;
	std::string body;

	if (fHttp->Get(OrcidUrl(person->orcid, "person"), &body, Json()) == B_OK && ParseOrcidPerson(body, person))
		known = true;
	if (fHttp->Get(OrcidUrl(person->orcid, "employments"), &body, Json()) == B_OK && ParseOrcidEmployments(body, person))
		known = true;

	// the entry of Wikidata with this iD, if the person is notable enough to have one
	std::string wikidataId;
	if (fHttp->Get(WikidataSearchUrl(person->orcid), &body, Json()) == B_OK && ParseWikidataSearch(body, &wikidataId)
			&& fHttp->Get(WikidataEntityUrl(wikidataId), &body, Json()) == B_OK && ParseWikidataEntity(body, wikidataId, person)) {
		known = true;
		if (!person->photoFile.empty() && fHttp->Get(CommonsImageUrl(person->photoFile), &body, Json()) == B_OK
				&& ParseCommonsImage(body, person) && fHttp->Get(person->photoUrl, photo, Image()) != B_OK) {
			spdlog::info("could not fetch the photo of {}", person->name);
			photo->clear();
			person->photoUrl.clear();
		}
	}
	if (!known)
		spdlog::info("ORCID knows nothing public about {}", person->orcid);
	return known;
}


/** The person that is the author: the one that has the iD in `schema:sameAs` (anywhere on the volume of the paper), else the file with the name next to the paper. */
bool
AuthorMaker::_FindByOrcid(const std::string& orcid, const entry_ref& near, entry_ref* found)
{
	BVolume volume(near.device);
	if (volume.InitCheck() != B_OK || !volume.KnowsQuery())
		return false;

	index_info info;
	if (fs_stat_index(near.device, sen::onto::people::attr::kSameAs, &info) != 0)
		fs_create_index(near.device, sen::onto::people::attr::kSameAs, B_STRING_TYPE, 0);

	BString predicate;
	predicate << sen::onto::people::attr::kSameAs << "==\"*" << orcid.c_str() << "*\"";
	BQuery query;
	if (query.SetVolume(&volume) != B_OK || query.SetPredicate(predicate.String()) != B_OK || query.Fetch() != B_OK)
		return false;

	entry_ref hit;
	while (query.GetNextRef(&hit) == B_OK) {
		BNode node(&hit);
		char type[B_MIME_TYPE_LENGTH];
		type[0] = '\0';
		BNodeInfo(&node).GetType(type);
		if (strcmp(type, sen::onto::people::mime::kPerson) == 0) {
			*found = hit;
			return true;
		}
	}
	return false;
}


status_t
AuthorMaker::_FindOrCreate(const entry_ref& paper, const PersonData& person, entry_ref* found, bool* created)
{
	if (!person.orcid.empty() && _FindByOrcid(person.orcid, paper, found)) {
		*created = false;
		return B_OK;
	}

	// (the Library of the current context, where a new person is made; a person of that name there, or next to the paper where an earlier version put it, is the same
	// one unless it is known to be another: another ORCID iD)
	if (person.name.empty())
		return B_BAD_VALUE;
	return sensei::FindOrCreateEntityFile(paper, sen::onto::people::mime::kPerson, person.name.c_str(), person.orcid.empty() ? "author" : person.orcid.c_str(),
		[&person](BNode& node) {
			BString sameAs;
			node.ReadAttrString(sen::onto::people::attr::kSameAs, &sameAs);
			const bool other = !sameAs.IsEmpty() && !person.orcid.empty() && sameAs.FindFirst("orcid.org/") >= 0
				&& sameAs.FindFirst(person.orcid.c_str()) < 0;
			return !other;
		}, found, created);
}


void
AuthorMaker::_WriteAttributes(const entry_ref& ref, const PersonData& data)
{
	BNode node(&ref);
	if (node.InitCheck() != B_OK)
		return;

	auto put = [&](const char* attribute, const std::string& value) {
		if (value.empty() || (!fOverwrite && HasAttr(node, attribute)))
			return;
		node.WriteAttr(attribute, B_STRING_TYPE, 0, value.c_str(), value.size() + 1);
	};
	namespace people = sen::onto::people::attr;

	put(people::kName, data.name);
	put(people::kGivenName, data.givenName);
	put(people::kFamilyName, data.familyName);
	put(people::kDescription, data.biography);
	put(people::kJobTitle, data.jobTitle);
	put(people::kHomepage, data.homepage);

	// the iDs are added to the ones that are there, a list
	if (!data.sameAs.empty()) {
		BString existing;
		node.ReadAttrString(people::kSameAs, &existing);
		std::vector<std::string> values = sen::list::Split(existing.String(), ',');
		for (const std::string& iri : data.sameAs) {
			if (std::find(values.begin(), values.end(), iri) == values.end())
				values.push_back(iri);
		}
		const std::string joined = sen::list::Join(values, ',');
		node.WriteAttr(people::kSameAs, B_STRING_TYPE, 0, joined.c_str(), joined.size() + 1);
	}

	time_t birthday = ParseDate(data.birthDate);
	if (birthday != (time_t) -1 && (fOverwrite || !HasAttr(node, people::kBirthDate)))
		node.WriteAttr(people::kBirthDate, B_TIME_TYPE, 0, &birthday, sizeof(birthday));

	// the photo is somebody else's work: where it is, who made it and under which license
	if (!data.photoUrl.empty()) {
		put(people::kImage, data.photoPage);
		put(people::kPhotoLicense, data.photoLicense);
		put(people::kPhotoCredit, data.photoCredit);
	}
	node.Sync();
}


/** the person is the author of the paper: the server writes the relation (and its opposite direction, "authored by") */
status_t
AuthorMaker::_Relate(const entry_ref& person, const entry_ref& paper)
{
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid())
		return B_NOT_INITIALIZED;		// the SEN server is not running: the person is there, the relation is made by the next enrichment

	BMessage add(sen::cmd::kRelationAdd), answer;
	add.AddString(sen::key::kRelationType, sen::onto::books::mime::kAuthorship);
	add.AddRef(sen::key::kSourceRef, &person);
	add.AddRef(sen::key::kTargetRef, &paper);
	status_t result = server.SendMessage(&add, &answer, 10000000, 10000000);
	if (result != B_OK)
		return result;
	if (answer.GetInt32(sen::key::kStatus, 0) >= 400) {
		spdlog::warn("the SEN server: {}", answer.GetString(sen::key::kDetail, "no relation"));
		return B_ERROR;
	}
	return B_OK;
}
