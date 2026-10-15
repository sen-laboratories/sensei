/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperStore.h"

#include <ctime>

#include <Entry.h>
#include <MimeType.h>
#include <NodeInfo.h>
#include <Query.h>
#include <String.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_attr.h>
#include <fs_index.h>

#include <sen/Sen.h>
#include <spdlog/spdlog.h>

#include "PaperDate.h"
#include "PaperDoi.h"
#include "PaperKeys.h"

namespace paperboy {
namespace store {

namespace {

/** cap of the cited works that the state keeps: a survey with thousands of references would make a huge attribute */
constexpr size_t kMaxStoredReferences = 500;

bool
HasValue(BNode& node, const char* name)
{
	attr_info info;
	return node.GetAttrInfo(name, &info) == B_OK && info.size > 1;
}

status_t
Check(ssize_t written, ssize_t expected)
{
	if (written < 0)
		return (status_t) written;
	return written == expected ? B_OK : B_ERROR;
}

status_t
PutString(BNode& node, const char* name, const std::string& value, bool overwrite)
{
	if (value.empty())
		return B_OK;
	if (!overwrite && HasValue(node, name)) {
		spdlog::debug("keeping {}", name);
		return B_OK;
	}
	return Check(node.WriteAttr(name, B_STRING_TYPE, 0, value.c_str(), value.size() + 1), value.size() + 1);
}

status_t
PutMessage(BNode& node, const char* name, const BMessage& message)
{
	ssize_t size = message.FlattenedSize();
	std::vector<char> buffer(size);
	status_t status = message.Flatten(buffer.data(), size);
	if (status != B_OK)
		return status;
	return Check(node.WriteAttr(name, B_MESSAGE_TYPE, 0, buffer.data(), size), size);
}

/** the data of the access is replaced by every run, so a link that is gone does not stay */
void
Replace(BNode& node, const char* name, const std::string& value)
{
	if (value.empty())
		node.RemoveAttr(name);
	else
		PutString(node, name, value, true);
}

std::string
Join(const std::vector<std::string>& values)
{
	std::string joined;
	for (const std::string& value : values)
		joined += (joined.empty() ? "" : ";") + value;
	return joined;
}

BMessage
AuthorsMessage(const Paper& paper)
{
	BMessage authors;
	for (const Author& author : paper.authors) {
		BMessage person;
		person.AddString(key::kName, author.name.c_str());
		if (!author.orcid.empty())
			person.AddString(key::kOrcid, ("https://orcid.org/" + author.orcid).c_str());
		for (const std::string& affiliation : author.affiliations)
			person.AddString(key::kAffiliation, affiliation.c_str());
		authors.AddMessage(key::kPerson, &person);
	}
	return authors;
}

BMessage
EncodingMessage(const Paper& paper)
{
	BMessage encodings;
	for (const Location& location : paper.locations) {
		BMessage object;
		object.AddString(key::kContentUrl, location.Url().c_str());
		object.AddString(key::kFormat, location.pdfUrl.empty() ? "text/html" : "application/pdf");
		object.AddString(key::kSource, location.source.c_str());
		if (!paper.oaStatus.empty())
			object.AddString(key::kOaStatus, paper.oaStatus.c_str());
		if (!location.version.empty())
			object.AddString(key::kVersion, location.version.c_str());
		if (!location.license.empty())
			object.AddString(key::kLicense, location.license.c_str());
		object.AddBool(key::kIsBest, location.isBest);
		encodings.AddMessage(key::kMediaObject, &object);
	}
	return encodings;
}

BMessage
StateMessage(const char* status, const char* doiSource, const std::string& openAlexId, bool unpaywall,
	const std::vector<Reference>& references)
{
	BMessage state;
	state.AddString(key::kStatus, status);
	state.AddInt64(key::kChecked, (int64) time(NULL));
	if (doiSource != NULL && doiSource[0] != '\0')
		state.AddString(key::kDoiSource, doiSource);
	if (!openAlexId.empty())
		state.AddString(key::kOpenAlexId, openAlexId.c_str());
	state.AddBool(key::kUnpaywall, unpaywall);
	for (size_t i = 0; i < references.size() && i < kMaxStoredReferences; i++) {
		BMessage reference;
		reference.AddString(key::kRefId, references[i].openAlexId.c_str());
		reference.AddString(key::kRefDoi, references[i].doi.c_str());
		reference.AddString(key::kRefTitle, references[i].title.c_str());
		reference.AddInt32(key::kRefYear, references[i].year);
		state.AddMessage(key::kReference, &reference);
	}
	return state;
}

bool
ReadMessage(BNode& node, const char* name, BMessage* message)
{
	attr_info info;
	if (node.GetAttrInfo(name, &info) != B_OK || info.type != B_MESSAGE_TYPE || info.size <= 0)
		return false;
	std::vector<char> buffer(info.size);
	if (node.ReadAttr(name, B_MESSAGE_TYPE, 0, buffer.data(), info.size) != info.size)
		return false;
	return message->Unflatten(buffer.data()) == B_OK;
}

}	// namespace


std::string
StoredDoi(BNode& node)
{
	BString value;
	if (node.ReadAttrString(attr::kIdentifier, &value) != B_OK)
		return "";
	return NormalizeDoi(value.String());
}


bool
StoredReferences(BNode& node, std::vector<Reference>* references)
{
	BMessage state;
	if (!ReadMessage(node, attr::kState, &state))
		return false;

	type_code type;
	int32 count;
	if (state.GetInfo(key::kReference, &type, &count) != B_OK)
		return false;
	for (int32 i = 0; i < count; i++) {
		BMessage entry;
		if (state.FindMessage(key::kReference, i, &entry) != B_OK)
			continue;
		Reference reference;
		reference.openAlexId = entry.GetString(key::kRefId, "");
		reference.doi = entry.GetString(key::kRefDoi, "");
		reference.title = entry.GetString(key::kRefTitle, "");
		reference.year = entry.GetInt32(key::kRefYear, 0);
		references->push_back(reference);
	}
	return true;
}


status_t
SetSemanticType(const entry_ref& file)
{
	BNode node(&file);
	status_t status = node.InitCheck();
	if (status != B_OK)
		return status;

	BNodeInfo info(&node);
	char haikuType[B_MIME_TYPE_LENGTH];
	if (info.GetType(haikuType) != B_OK || haikuType[0] == '\0') {
		BMimeType guess;
		if (BMimeType::GuessMimeType(&file, &guess) == B_OK)
			info.SetType(guess.Type());
	}

	BString current;
	if (node.ReadAttrString(sen::attr::kType, &current) == B_OK && current == kSemanticType)
		return B_OK;

	status = Check(node.WriteAttr(sen::attr::kType, B_MIME_STRING_TYPE, 0, kSemanticType, sizeof(kSemanticType)),
		sizeof(kSemanticType));
	if (status != B_OK)
		spdlog::warn("could not set {} of {}: {}", sen::attr::kType, file.name, strerror(status));
	else
		spdlog::info("{} of {} is {}", sen::attr::kType, file.name, kSemanticType);
	return status;
}


status_t
WritePaper(BNode& node, const Paper& paper, const char* doiSource, bool unpaywall, bool overwrite,
	const std::vector<Reference>& references)
{
	status_t result = node.InitCheck();
	if (result != B_OK)
		return result;

	// the DOI: another kind of identifier that someone put there stays
	BString existing;
	bool foreign = node.ReadAttrString(attr::kIdentifier, &existing) == B_OK && !existing.IsEmpty()
		&& NormalizeDoi(existing.String()).empty();
	if (foreign && !overwrite)
		spdlog::warn("{} holds '{}', not a DOI: keeping it", attr::kIdentifier, existing.String());
	else
		PutString(node, attr::kIdentifier, paper.doi, true);

	// bibliographic data: fills what is missing
	PutString(node, attr::kTitle, paper.title, overwrite);
	PutString(node, attr::kType, paper.type, overwrite);
	PutString(node, attr::kLanguage, paper.language, overwrite);
	PutString(node, attr::kPublisher, paper.publisher, overwrite);
	PutString(node, attr::kIsPartOf, paper.venue, overwrite);
	PutString(node, attr::kDescription, paper.abstract, overwrite);
	PutString(node, attr::kSubject, Join(paper.keywords), overwrite);

	std::vector<std::string> names;
	for (const Author& author : paper.authors)
		names.push_back(author.name);
	PutString(node, attr::kCreator, Join(names), overwrite);

	time_t date = ParseDate(paper.date);
	if (date != (time_t) -1 && (overwrite || !HasValue(node, attr::kDate))) {
		result = Check(node.WriteAttr(attr::kDate, B_TIME_TYPE, 0, &date, sizeof(date)), sizeof(date));
		if (result != B_OK)
			spdlog::warn("could not write {}: {}", attr::kDate, strerror(result));
	}

	if (!paper.authors.empty() && (overwrite || !HasValue(node, attr::kAuthor)))
		PutMessage(node, attr::kAuthor, AuthorsMessage(paper));
	else if (paper.authors.empty())
		spdlog::debug("no authors to write");

	// access: replaced on every run
	const Location* best = paper.BestLocation();
	std::string doiUrl = paper.doi.empty() ? "" : "https://doi.org/" + paper.doi;
	Replace(node, attr::kUrl, best != NULL && !best->landingUrl.empty() ? best->landingUrl : doiUrl);
	Replace(node, attr::kContentUrl, best != NULL ? best->pdfUrl : "");
	Replace(node, attr::kOaStatus, paper.oaStatus.empty() && paper.fromOpenAlex ? "closed" : paper.oaStatus);
	if (paper.locations.empty())
		node.RemoveAttr(attr::kEncoding);
	else
		PutMessage(node, attr::kEncoding, EncodingMessage(paper));
	if (paper.citedByCount >= 0) {
		int32 count = paper.citedByCount;
		node.WriteAttr(attr::kCitationCount, B_INT32_TYPE, 0, &count, sizeof(count));
	}

	result = PutMessage(node, attr::kState,
		StateMessage(outcome::kOk, doiSource, paper.openAlexId, unpaywall, references));
	node.Sync();
	return result;
}


status_t
WriteNotFound(BNode& node, const char* statusName)
{
	return PutMessage(node, attr::kState, StateMessage(statusName, "", "", false, std::vector<Reference>()));
}


void
EnsureIndex(const entry_ref& file)
{
	dev_t device = file.device;
	index_info info;
	if (fs_stat_index(device, attr::kIdentifier, &info) == 0)
		return;
	if (fs_create_index(device, attr::kIdentifier, B_STRING_TYPE, 0) == 0)
		spdlog::info("made the index {} on the volume of {}", attr::kIdentifier, file.name);
	else
		spdlog::debug("no index {} on the volume of {}: {}", attr::kIdentifier, file.name, strerror(errno));
}


bool
FindLocalFile(const std::string& doi, const entry_ref& near, entry_ref* found)
{
	BString predicate;
	predicate << attr::kIdentifier << "==\"" << doi.c_str() << "\"";

	auto search = [&](const BVolume& volume) {
		if (!volume.KnowsQuery() || !volume.KnowsAttr())
			return false;
		BQuery query;
		if (query.SetVolume(&volume) != B_OK || query.SetPredicate(predicate.String()) != B_OK || query.Fetch() != B_OK)
			return false;
		entry_ref hit;
		while (query.GetNextRef(&hit) == B_OK) {
			if (hit.device == near.device && hit.directory == near.directory && BString(hit.name) == near.name)
				continue;		// the paper itself
			*found = hit;
			return true;
		}
		return false;
	};

	BVolume own(near.device);
	if (own.InitCheck() == B_OK && search(own))
		return true;

	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK) {
		if (volume.Device() != near.device && search(volume))
			return true;
	}
	return false;
}

}	// namespace store
}	// namespace paperboy
