/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "Paperboy.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include <Directory.h>
#include <Entry.h>
#include <Node.h>
#include <Path.h>
#include <String.h>

#include <sen/Sen.h>
#include <sen/Sensei.h>
#include <spdlog/spdlog.h>

#include "PaperApi.h"
#include "PaperDoi.h"
#include "PaperJson.h"
#include "PaperKeys.h"
#include "PaperName.h"
#include "PaperStore.h"
#include "PaperTitle.h"

using namespace paperboy;

namespace {

/** the references of one paper that are looked up: the citations of a handbook can number thousands */
constexpr size_t kMaxReferences = 300;
/** a title that is sent to the search is a title, not a paragraph of the document: it leaves the computer */
constexpr size_t kMaxSearchTitleLength = 250;
/** a hit of the title search must be this like the title of the document */
constexpr double kMinTitleSimilarity = 0.85;
/** DOIs from the pages after the first one: only the first few, they are most likely cited papers */
constexpr size_t kMaxLaterDois = 2;

void
AddCandidate(std::vector<std::pair<std::string, const char*>>* list, const std::string& doi, const char* source)
{
	if (doi.empty())
		return;
	for (const auto& entry : *list) {
		if (entry.first == doi)
			return;
	}
	list->push_back({doi, source});
}

/** one line: runs of blanks and line breaks become one blank */
std::string
OneLine(const std::string& text)
{
	std::string out;
	bool blank = true;
	for (unsigned char c : text) {
		if (std::isspace(c)) {
			if (!blank)
				out += ' ';
			blank = true;
		} else {
			out += (char) c;
			blank = false;
		}
	}
	if (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}

/** why the services could not be asked, for people */
std::string
Unreachable(status_t status)
{
	switch (status) {
		case B_BUSY:
			return "OpenAlex asks to slow down, or the free daily budget of this computer's address is used up. A free key "
				"(openalex.org/settings/api) in the settings of Paperboy (api_key) gives a budget of your own.";
		case B_NOT_ALLOWED:
			return "OpenAlex refused the request: check the api_key in the settings of Paperboy.";
		default:
			return std::string("The services could not be reached: ") + strerror(status);
	}
}

/** the extension of a file name with its dot (".pdf"), "" if it has none that looks like one */
std::string
Extension(const char* name)
{
	const char* dot = strrchr(name, '.');
	return (dot != NULL && dot != name && strlen(dot) <= 6) ? dot : "";
}

}	// namespace


Paperboy::Paperboy(const Settings& settings)
	:
	fSettings(settings),
	fAccess{settings.email, settings.apiKey},
	fHttp(UserAgent(settings.email))
{
}


std::vector<Paperboy::Candidate>
Paperboy::_Candidates(const entry_ref& file, DocumentFacts* facts, bool* factsRead)
{
	*factsRead = false;
	std::vector<std::pair<std::string, const char*>> found;

	BNode node(&file);
	std::string stored = store::StoredDoi(node);
	if (!stored.empty()) {
		AddCandidate(&found, stored, source::kAttribute);
	} else {
		BPath path(&file);
		status_t status = ReadDocumentFacts(path.Path(), fSettings.maxPages, facts);
		if (status == B_OK) {
			*factsRead = true;
			for (const std::string& field : facts->metadata) {
				for (const std::string& doi : FindDois(field))
					AddCandidate(&found, doi, source::kMetadata);
			}
			if (!facts->pages.empty()) {
				for (const std::string& doi : FindDois(facts->pages.front()))
					AddCandidate(&found, doi, source::kFirstPage);
				for (const std::string& id : FindArxivIds(facts->pages.front()))
					AddCandidate(&found, ArxivDoi(id), source::kArxiv);
			}
			size_t later = 0;
			for (size_t page = 1; page < facts->pages.size() && later < kMaxLaterDois; page++) {
				for (const std::string& doi : FindDois(facts->pages[page])) {
					if (later++ < kMaxLaterDois)
						AddCandidate(&found, doi, source::kLaterPage);
				}
			}
		} else {
			spdlog::warn("could not read {}: {}", file.name, strerror(status));
		}
	}

	std::vector<Candidate> candidates;
	for (const auto& entry : found)
		candidates.push_back({entry.first, entry.second});
	return candidates;
}


status_t
Paperboy::_LookUp(const std::string& doi, Paper* paper, bool withAccess)
{
	std::string body;
	status_t status = fHttp.Get(OpenAlexWorkUrl(doi, fAccess), &body);
	if (status != B_OK)
		return status;
	if (!ParseOpenAlexWork(body, paper))
		return B_BAD_DATA;
	if (paper->doi.empty())
		paper->doi = doi;

	if (withAccess && fSettings.unpaywall) {
		if (fSettings.email.empty()) {
			spdlog::info("no e-mail address in the settings: the open access data of OpenAlex is used (Unpaywall asks for one)");
		} else {
			status = fHttp.Get(UnpaywallUrl(paper->doi, fSettings.email), &body);
			if (status == B_OK) {
				if (!ParseUnpaywall(body, paper))
					spdlog::warn("Unpaywall answered with something that is not a DOI record");
			} else if (status == B_ENTRY_NOT_FOUND) {
				spdlog::info("Unpaywall does not know {} (only DOIs of Crossref), keeping the data of OpenAlex", paper->doi);
			} else {
				spdlog::warn("Unpaywall could not be asked: {}", strerror(status));
			}
		}
	}
	return B_OK;
}


std::string
Paperboy::_FindByTitle(const DocumentFacts& facts)
{
	std::vector<std::string> titles;
	for (const std::string& title : { facts.infoTitle, facts.headline }) {
		if (LooksLikeTitle(title) && title.size() <= kMaxSearchTitleLength)
			titles.push_back(title);
	}

	for (const std::string& title : titles) {
		std::string body;
		if (fHttp.Get(OpenAlexSearchUrl(title, fAccess), &body) != B_OK)
			continue;
		for (const Reference& hit : ParseOpenAlexList(body)) {
			if (!hit.doi.empty() && TitleSimilarity(title, hit.title) >= kMinTitleSimilarity) {
				spdlog::info("title '{}' matches '{}' ({})", title, hit.title, hit.doi);
				return hit.doi;
			}
		}
	}
	return "";
}


void
Paperboy::_FetchReferences(const Paper& paper, std::vector<Reference>* references)
{
	std::vector<std::string> ids = paper.referencedWorks;
	if (ids.size() > kMaxReferences) {
		spdlog::info("{} cites {} works, the first {} are looked up", paper.doi, ids.size(), kMaxReferences);
		ids.resize(kMaxReferences);
	}

	std::vector<Reference> known;
	for (size_t start = 0; start < ids.size(); start += kBatchSize) {
		std::vector<std::string> batch(ids.begin() + start, ids.begin() + std::min(ids.size(), start + kBatchSize));
		std::string body;
		status_t status = fHttp.Get(OpenAlexBatchUrl(batch, fAccess), &body);
		if (status != B_OK) {
			spdlog::warn("could not look up the cited works: {}", strerror(status));
			break;
		}
		for (const Reference& reference : ParseOpenAlexList(body))
			known.push_back(reference);
	}

	// in the order of the paper, a work that the service did not return stays with its id alone
	for (const std::string& id : ids) {
		auto hit = std::find_if(known.begin(), known.end(), [&](const Reference& r) { return r.openAlexId == id; });
		if (hit != known.end()) {
			references->push_back(*hit);
		} else {
			Reference reference;
			reference.openAlexId = id;
			references->push_back(reference);
		}
	}
}


bool
Paperboy::_Rename(entry_ref* file, const Paper& paper)
{
	std::string name = BuildFileName(fSettings.renamePattern, paper, Extension(file->name), fSettings.renameUnderscores);
	if (name.empty() || name == file->name)
		return false;

	BEntry entry(file);
	std::string extension = Extension(name.c_str());
	std::string stem = name.substr(0, name.size() - extension.size());
	const char* separator = fSettings.renameUnderscores ? "_" : " ";
	for (int attempt = 1; attempt <= 9; attempt++) {
		std::string candidate = attempt == 1 ? name : stem + separator + std::to_string(attempt) + extension;
		if (candidate == file->name)
			return false;
		status_t status = entry.Rename(candidate.c_str(), false);
		if (status == B_OK) {
			entry.GetRef(file);
			spdlog::info("renamed to {}", candidate);
			return true;
		}
		if (status != B_FILE_EXISTS) {
			spdlog::warn("could not rename {} to {}: {}", file->name, candidate, strerror(status));
			return false;
		}
	}
	return false;
}


std::string
Paperboy::_DocumentTitle(const DocumentFacts& facts) const
{
	for (const std::string& candidate : { facts.infoTitle, facts.headline }) {
		std::string title = OneLine(candidate);
		if (LooksLikeTitle(title) && title.size() <= kMaxSearchTitleLength)
			return title;
	}
	return "";
}


status_t
Paperboy::PrintFacts(const entry_ref& file)
{
	DocumentFacts facts;
	BPath path(&file);
	status_t status = ReadDocumentFacts(path.Path(), fSettings.maxPages, &facts);
	if (status != B_OK) {
		std::printf("could not read %s: %s\n", file.name, strerror(status));
		return status;
	}
	std::printf("pages: %d\ntitle field: %s\nheadline: %s\n", facts.pageCount, facts.infoTitle.c_str(), facts.headline.c_str());
	for (const std::string& field : facts.metadata)
		std::printf("field: %s\n", field.c_str());
	std::printf("document title: %s\n", _DocumentTitle(facts).c_str());
	for (const std::string& doi : (facts.pages.empty() ? std::vector<std::string>() : FindDois(facts.pages.front())))
		std::printf("DOI on the first page: %s\n", doi.c_str());
	if (!facts.pages.empty())
		std::printf("--- first page (start)\n%s\n", facts.pages.front().substr(0, 1500).c_str());
	return B_OK;
}


status_t
Paperboy::Identify(const entry_ref& file, BMessage* reply)
{
	DocumentFacts facts;
	bool factsRead;
	std::vector<Candidate> candidates = _Candidates(file, &facts, &factsRead);

	// a DOI that the file itself states says "paper"; one that turns up on later pages is more likely a citation
	for (const Candidate& candidate : candidates) {
		if (std::string(candidate.source) == source::kLaterPage)
			continue;
		status_t status = store::SetSemanticType(file);
		if (status != B_OK)
			return status;
		reply->AddString(sensei::key::kSemanticType, kSemanticType);
		reply->AddString(attr::kIdentifier, candidate.doi.c_str());
		spdlog::info("{} is a paper: {} ({})", file.name, candidate.doi, candidate.source);
		return B_OK;
	}
	spdlog::info("{} has no DOI of its own", file.name);
	return B_ENTRY_NOT_FOUND;
}


status_t
Paperboy::Enrich(entry_ref* file, BMessage* reply, bool wipe)
{
	fSummary.clear();
	fSummaryTitle = "Paper not enriched";
	const bool overwrite = wipe || fSettings.overwrite;

	DocumentFacts facts;
	bool factsRead;
	std::vector<Candidate> candidates = _Candidates(*file, &facts, &factsRead);

	Paper paper;
	std::string doiSource;
	bool found = false;
	for (const Candidate& candidate : candidates) {
		Paper attempt;
		status_t status = _LookUp(candidate.doi, &attempt, true);
		if (status == B_OK) {
			paper = attempt;
			doiSource = candidate.source;
			found = true;
			break;
		}
		if (status != B_ENTRY_NOT_FOUND) {
			// the services could not be asked: say nothing about the file
			spdlog::error("lookup of {} failed: {}", candidate.doi, strerror(status));
			fSummary = Unreachable(status);
			return status;
		}
		spdlog::info("OpenAlex does not know {} ({})", candidate.doi, candidate.source);
	}

	if (!factsRead && !found && (fSettings.titleSearch || fSettings.rename)) {
		BPath path(file);
		factsRead = ReadDocumentFacts(path.Path(), fSettings.maxPages, &facts) == B_OK;
	}

	if (!found && fSettings.titleSearch && factsRead) {
		std::string doi = _FindByTitle(facts);
		if (!doi.empty()) {
			Paper attempt;
			status_t status = _LookUp(doi, &attempt, true);
			if (status == B_OK) {
				paper = attempt;
				doiSource = source::kTitle;
				found = true;
			} else if (status != B_ENTRY_NOT_FOUND) {
				fSummary = Unreachable(status);
				return status;
			}
		}
	}

	BNode node(file);
	if (!found) {
		// no service knows the paper; the document may still say what it is called
		std::string title = factsRead ? _DocumentTitle(facts) : "";
		if (title.empty()) {
			store::WriteNotFound(node, candidates.empty() ? outcome::kDoiNotFound : outcome::kNotInOpenAlex);
			fSummary = std::string(candidates.empty() ? "No DOI and no title that OpenAlex knows in " : "OpenAlex does not know the DOIs of ")
				+ file->name + ".";
			return B_ENTRY_NOT_FOUND;
		}

		status_t status = store::WriteDocumentTitle(node, title, overwrite);
		if (status != B_OK) {
			fSummary = std::string("Could not write the title of ") + file->name + ": " + strerror(status);
			return status;
		}
		Paper named;
		named.title = title;
		bool renamed = fSettings.rename && _Rename(file, named);
		if (renamed)
			reply->AddRef("refs", file);
		reply->AddString(attr::kTitle, title.c_str());
		fSummaryTitle = "Title from the document";
		fSummary = "'" + title + "' is not in OpenAlex: the title is the one of the document" + (renamed ? ", the file is named after it." : ".");
		spdlog::info("{}", fSummary);
		return B_OK;
	}

	std::vector<Reference> references;
	_FetchReferences(paper, &references);

	store::EnsureIndex(*file);
	status_t status = store::WritePaper(node, paper, doiSource.c_str(), paper.fromUnpaywall, overwrite, references);
	if (status != B_OK) {
		spdlog::error("could not write the attributes of {}: {}", file->name, strerror(status));
		fSummary = std::string("Could not write the attributes of ") + file->name + ": " + strerror(status);
		return status;
	}
	store::SetSemanticType(*file);

	bool renamed = fSettings.rename && _Rename(file, paper);
	if (renamed)
		reply->AddRef("refs", file);

	reply->AddString(sensei::key::kSemanticType, kSemanticType);
	reply->AddString(attr::kIdentifier, paper.doi.c_str());
	reply->AddString(attr::kTitle, paper.title.c_str());
	fSummaryTitle = "Paper enriched";
	fSummary = "'" + paper.title + "'" + (paper.oaStatus.empty() ? "" : ", open access: " + paper.oaStatus) + ", "
		+ std::to_string(references.size()) + " cited works" + (renamed ? "; the file is named after the title." : ".");
	spdlog::info("{}: {} ({})", file->name, fSummary, doiSource);
	return B_OK;
}


status_t
Paperboy::Extract(const entry_ref& file, bool self, BMessage* reply)
{
	// a paper contains nothing that is a relation to itself: the server gets an empty answer
	if (self)
		return B_OK;

	std::vector<Reference> references;
	BNode node(&file);
	if (!store::StoredReferences(node, &references)) {
		// not enriched yet: ask OpenAlex for the cited works, but store nothing (an extractor does not write)
		DocumentFacts facts;
		bool factsRead;
		std::vector<Candidate> candidates = _Candidates(file, &facts, &factsRead);
		for (const Candidate& candidate : candidates) {
			Paper paper;
			status_t status = _LookUp(candidate.doi, &paper, false);
			if (status == B_OK) {
				_FetchReferences(paper, &references);
				break;
			}
			if (status != B_ENTRY_NOT_FOUND)
				return status;
		}
	}

	// one item with an entry per cited work in each field (the server wants the same count in all of them)
	BMessage item;
	int32 count = 0;
	for (const Reference& reference : references) {
		entry_ref local;
		bool isLocal = !reference.doi.empty() && store::FindLocalFile(reference.doi, file, &local);
		if (fSettings.localOnly && !isLocal)
			continue;

		std::string location;
		if (isLocal)
			location = BPath(&local).Path();
		else if (!reference.doi.empty())
			location = "https://doi.org/" + reference.doi;
		else
			location = "https://openalex.org/" + reference.openAlexId;

		std::string label = !reference.title.empty() ? reference.title : (!reference.doi.empty() ? reference.doi : reference.openAlexId);
		item.AddString(sensei::key::kLabel, label.c_str());
		item.AddString(itemfield::kPath, location.c_str());
		item.AddString(itemfield::kDoi, reference.doi.c_str());
		item.AddString(itemfield::kTitle, reference.title.c_str());
		item.AddInt32(itemfield::kYear, reference.year);
		count++;
	}
	if (count > 0)
		reply->AddMessage(sensei::key::kItem, &item);

	spdlog::info("{} cites {} works{}", file.name, count, fSettings.localOnly ? " on this computer" : "");
	return B_OK;
}
