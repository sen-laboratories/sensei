/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "../../common/EntityFile.h"
#include "PaperInstitutions.h"

#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Query.h>
#include <String.h>
#include <Volume.h>
#include <fs_attr.h>
#include <fs_index.h>

#include <algorithm>
#include <string.h>

#include <sen/Sen.h>
#include <sen/SenList.h>
#include <sen/SenMessages.h>
#include <sen/SenOntoCore.h>
#include <sen/SenOntoPapers.h>
#include <sen/SenOntoPeople.h>
#include <spdlog/spdlog.h>

#include "PaperDate.h"

using namespace paperboy;

namespace {

namespace people = sen::onto::people::attr;

bool
HasAttr(BNode& node, const char* name)
{
	attr_info info;
	return node.GetAttrInfo(name, &info) == B_OK && info.size > 1;
}

}	// namespace


InstitutionMaker::InstitutionMaker()
	:
	fMade(0)
{
}


int32
InstitutionMaker::Affiliate(const entry_ref& person, const entry_ref& paper, const std::vector<Stay>& stays)
{
	int32 related = 0;
	for (const Stay& stay : stays) {
		if (stay.rorId.empty() || stay.name.empty())
			continue;

		entry_ref institution;
		bool created = false;
		status_t result = _FindOrCreate(paper, stay, &institution, &created);
		if (result != B_OK) {
			spdlog::warn("could not make the institution {}: {}", stay.name, strerror(result));
			continue;
		}
		if (created) {
			fMade++;
			spdlog::info("making institution '{}' ({})", stay.name, stay.rorId);
		}
		_WriteAttributes(institution, stay);

		result = _Relate(person, institution, stay);
		if (result == B_OK)
			related++;
		else if (result != B_NOT_INITIALIZED)
			spdlog::warn("could not relate {} to {}: {}", person.name, stay.name, strerror(result));

		if (stay.namedByPaper) {
			result = _RelatePaper(paper, institution);
			if (result != B_OK && result != B_NOT_INITIALIZED)
				spdlog::warn("could not relate the paper to {}: {}", stay.name, strerror(result));
		}
	}
	return related;
}


/** The institution with the id (in `dc:identifier`, bare) of the volume of the paper; the index is the one that the ontology installer makes. */
bool
InstitutionMaker::_FindByRor(const std::string& rorId, const entry_ref& near, entry_ref* found)
{
	BVolume volume(near.device);
	if (volume.InitCheck() != B_OK || !volume.KnowsQuery())
		return false;

	index_info info;
	if (fs_stat_index(near.device, people::kRor, &info) != 0)
		fs_create_index(near.device, people::kRor, B_STRING_TYPE, 0);

	BString predicate;
	predicate << people::kRor << "==\"" << rorId.c_str() << "\"";
	BQuery query;
	if (query.SetVolume(&volume) != B_OK || query.SetPredicate(predicate.String()) != B_OK || query.Fetch() != B_OK)
		return false;

	entry_ref hit;
	while (query.GetNextRef(&hit) == B_OK) {
		BNode node(&hit);
		char type[B_MIME_TYPE_LENGTH];
		type[0] = '\0';
		BNodeInfo(&node).GetType(type);
		if (strcmp(type, sen::onto::people::mime::kInstitution) == 0) {
			*found = hit;
			return true;
		}
	}
	return false;
}


status_t
InstitutionMaker::_FindOrCreate(const entry_ref& paper, const Stay& stay, entry_ref* found, bool* created)
{
	if (_FindByRor(stay.rorId, paper, found)) {
		*created = false;
		return B_OK;
	}

	// (a file of that name that has no id is the institution that somebody made by hand; one with another id is another institution)
	return sensei::FindOrCreateEntityFile(paper, sen::onto::people::mime::kInstitution, stay.name.c_str(), stay.rorId.c_str(),
		[&stay](BNode& node) {
			BString id;
			node.ReadAttrString(people::kRor, &id);
			return id.IsEmpty() || id == stay.rorId.c_str();
		}, found, created);
}


void
InstitutionMaker::_WriteAttributes(const entry_ref& ref, const Stay& stay)
{
	BNode node(&ref);
	if (node.InitCheck() != B_OK)
		return;

	// only what the file lacks: an institution that was enriched or written by hand is not overwritten by what a paper says of it
	auto put = [&](const char* attribute, const std::string& value) {
		if (value.empty() || HasAttr(node, attribute))
			return;
		node.WriteAttr(attribute, B_STRING_TYPE, 0, value.c_str(), value.size() + 1);
	};
	put(people::kName, stay.name);
	put(people::kRor, stay.rorId);
	put(people::kCountry, stay.country);
	put(people::kLocality, stay.city);
	put(people::kInstitutionType, stay.type);

	// the link to the registry is the same iD, the link of the list that the file has
	const std::string registry = "https://ror.org/" + stay.rorId;
	BString existing;
	node.ReadAttrString(people::kSameAs, &existing);
	std::vector<std::string> links = sen::list::Split(existing.String(), ',');
	if (std::find(links.begin(), links.end(), registry) == links.end()) {
		links.push_back(registry);
		const std::string joined = sen::list::Join(links, ',');
		node.WriteAttr(people::kSameAs, B_STRING_TYPE, 0, joined.c_str(), joined.size() + 1);
	}
	node.Sync();
}


/** the paper comes from the institution: stored at the paper only (an institution does not list its papers); the same relation again (another author of the same institution) is one */
status_t
InstitutionMaker::_RelatePaper(const entry_ref& paper, const entry_ref& institution)
{
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid())
		return B_NOT_INITIALIZED;

	BMessage add(sen::cmd::kRelationAdd), answer;
	add.AddString(sen::key::kRelationType, sen::onto::papers::mime::kSourceInstitution);
	add.AddRef(sen::key::kSourceRef, &paper);
	add.AddRef(sen::key::kTargetRef, &institution);
	status_t result = server.SendMessage(&add, &answer, 10000000, 10000000);
	if (result != B_OK)
		return result;
	if (answer.GetInt32(sen::key::kStatus, 0) >= 400) {
		spdlog::warn("the SEN server: {}", answer.GetString(sen::key::kDetail, "no relation"));
		return B_ERROR;
	}
	return B_OK;
}


/** the person is affiliated with the institution (stored at the person: the relation has no opposite direction) */
status_t
InstitutionMaker::_Relate(const entry_ref& person, const entry_ref& institution, const Stay& stay)
{
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid())
		return B_NOT_INITIALIZED;		// the SEN server is not running: the institution is there, the relation comes with the next enrichment

	BMessage properties;
	// (a period is as long as it is said: "2019" since the first moment of the year, until its last)
	const time_t since = ParseDate(stay.since);
	if (since != (time_t) -1)
		properties.AddData(people::kRelSince, B_TIME_TYPE, &since, sizeof(since));
	const time_t until = ParseDateEnd(stay.until);
	if (until != (time_t) -1)
		properties.AddData(people::kRelUntil, B_TIME_TYPE, &until, sizeof(until));
	if (!stay.role.empty())
		properties.AddString(sen::onto::core::attr::kRelRole, stay.role.c_str());
	if (!stay.note.empty())
		properties.AddString(people::kRelNote, stay.note.c_str());

	BMessage add(sen::cmd::kRelationAdd), answer;
	add.AddString(sen::key::kRelationType, sen::onto::people::mime::kAffiliation);
	add.AddRef(sen::key::kSourceRef, &person);
	add.AddRef(sen::key::kTargetRef, &institution);
	if (!properties.IsEmpty())
		add.AddMessage(sen::key::kRelationProperties, &properties);
	status_t result = server.SendMessage(&add, &answer, 10000000, 10000000);
	if (result != B_OK)
		return result;
	if (answer.GetInt32(sen::key::kStatus, 0) >= 400) {
		spdlog::warn("the SEN server: {}", answer.GetString(sen::key::kDetail, "no relation"));
		return B_ERROR;
	}
	return B_OK;
}
