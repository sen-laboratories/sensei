/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "Paperboy.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

#include <Directory.h>
#include <Entry.h>
#include <Node.h>
#include <Path.h>
#include <String.h>

#include <Application.h>
#include <Roster.h>

#include <sen/Sen.h>
#include <sen/SenAbout.h>
#include <sen/SenChoice.h>
#include <sen/Sensei.h>
#include <spdlog/spdlog.h>

#include "PaperApi.h"
#include "PaperAuthors.h"
#include "PaperChoice.h"
#include "PaperDoi.h"
#include "PaperJson.h"
#include "PaperKeys.h"
#include "PaperName.h"
#include "PaperSources.h"
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


status_t
Paperboy::_LookUpById(const std::string& openAlexId, Paper* paper)
{
	std::string body;
	status_t status = fHttp.Get(OpenAlexWorkByIdUrl(openAlexId, fAccess), &body);
	if (status != B_OK)
		return status;
	return ParseOpenAlexWork(body, paper) ? B_OK : B_BAD_DATA;
}


status_t
Paperboy::_FindByTitle(const DocumentFacts& facts, Reference* chosen)
{
	std::vector<std::string> titles;
	for (const std::string& title : { facts.infoTitle, facts.headline }) {
		if (LooksLikeTitle(title) && title.size() <= kMaxSearchTitleLength)
			titles.push_back(title);
	}

	// every hit that is like the title (once; a paper without a DOI is known by its id), with how good it is; the first title that finds any is the one
	struct Match {
		Reference	hit;
		double		score;
	};
	std::vector<Match> matches;
	std::set<std::string> seen;
	for (const std::string& title : titles) {
		std::string body;
		if (fHttp.Get(OpenAlexSearchUrl(title, fAccess), &body) != B_OK)
			continue;
		const std::vector<Reference> hits = ParseOpenAlexList(body);
		for (size_t rank = 0; rank < hits.size(); rank++) {
			const Reference& hit = hits[rank];
			const std::string& key = hit.doi.empty() ? hit.openAlexId : hit.doi;
			const double similarity = TitleSimilarity(title, hit.title);
			if (similarity < kMinTitleSimilarity || !seen.insert(key).second)
				continue;
			spdlog::info("title '{}' matches '{}' ({})", title, hit.title, key);
			matches.push_back({hit, ScoreHit(similarity, (int) rank, (int) hits.size(), hit.year)});
		}
		if (!matches.empty())
			break;
	}
	if (matches.empty())
		return B_ENTRY_NOT_FOUND;

	size_t best = 0;
	for (size_t i = 1; i < matches.size(); i++) {
		if (matches[i].score > matches[best].score)
			best = i;
	}
	if (matches.size() == 1 || !fSettings.choose) {
		*chosen = matches[best].hit;
		return B_OK;
	}

	// several papers have this title: the user says which one is it
	std::vector<sen::Candidate> candidates;
	for (const Match& match : matches) {
		sen::Candidate candidate;
		candidate.title = match.hit.title.empty() ? "(no title)" : match.hit.title.c_str();
		if (match.hit.year > 0)
			candidate.subtitle << match.hit.year;
		if (!match.hit.doi.empty())
			candidate.detail << "DOI " << match.hit.doi.c_str();
		else
			candidate.detail << "OpenAlex " << match.hit.openAlexId.c_str();
		candidate.source = match.hit.doi.empty() ? "OpenAlex" : SourceOfDoi(match.hit.doi).c_str();
		candidate.score = (float) match.score;
		candidates.push_back(candidate);
	}

	const int32 picked = _AskWhich(candidates);
	if (picked < 0 || picked >= (int32) matches.size())
		return B_CANCELED;

	*chosen = matches[picked].hit;
	spdlog::info("the user chose {}", chosen->doi.empty() ? chosen->openAlexId : chosen->doi);
	return B_OK;
}


int32
Paperboy::_AskWhich(const std::vector<sen::Candidate>& candidates)
{
	BBitmap* logo = NULL;
	app_info info;
	if (be_app != NULL && be_app->GetAppInfo(&info) == B_OK)
		logo = sen::LoadPluginLogo(info.signature);

	return sen::ChooseCandidate("Choose a paper", "Several papers have this title. Which one is it?", candidates, logo);
}


status_t
Paperboy::_FindElsewhere(const DocumentFacts& facts, bool factsRead, const std::vector<std::string>& arxivIds, Paper* found, const char** how)
{
	// each of them says how often it can be asked (arXiv: one request in three seconds), and they are asked once: a source that does not answer is skipped
	HttpOptions arxivOptions;
	arxivOptions.minInterval = 3000000;
	arxivOptions.accept = "application/atom+xml";
	arxivOptions.maxAttempts = 1;
	HttpOptions jsonOptions;
	jsonOptions.minInterval = 1000000;
	jsonOptions.maxAttempts = 1;

	// the identifier that the document names: that is the paper
	if (fSettings.arxiv) {
		for (const std::string& id : arxivIds) {
			std::string body;
			std::vector<Paper> papers;
			const status_t status = fHttp.Get(ArxivIdUrl(id), &body, arxivOptions);
			if (status == B_OK && ParseArxivFeed(body, &papers) && !papers.empty()) {
				*found = papers.front();
				*how = source::kArxiv;
				spdlog::info("arXiv knows {}: '{}'", id, found->title);
				return B_OK;
			}
			if (status != B_OK && status != B_ENTRY_NOT_FOUND)
				spdlog::info("arXiv did not answer for {}: {}", id, strerror(status));
		}
	}

	if (!fSettings.titleSearch || !factsRead)
		return B_ENTRY_NOT_FOUND;

	std::vector<std::string> titles;
	for (const std::string& title : { facts.infoTitle, facts.headline }) {
		if (LooksLikeTitle(title) && title.size() <= kMaxSearchTitleLength)
			titles.push_back(title);
	}
	if (titles.empty())
		return B_ENTRY_NOT_FOUND;

	struct Service {
		const char*		name;
		const char*		label;
		bool			enabled;
		std::string		(*address)(const std::string&);
		bool			(*parse)(const std::string&, std::vector<Paper>*);
		HttpOptions		options;
	};
	const Service services[] = {
		{service::kSemanticScholar, "Semantic Scholar", fSettings.semanticScholar, SemanticScholarSearchUrl, ParseSemanticScholarSearch, jsonOptions},
		{service::kDblp, "DBLP", fSettings.dblp, DblpSearchUrl, ParseDblpSearch, jsonOptions},
		{service::kArxiv, "arXiv", fSettings.arxiv, ArxivSearchUrl, ParseArxivFeed, arxivOptions},
	};

	struct Match {
		Paper	paper;
		double	score;
	};
	for (const Service& source : services) {
		if (!source.enabled)
			continue;

		std::vector<Match> matches;
		std::set<std::string> seen;
		for (const std::string& title : titles) {
			std::string body;
			const status_t status = fHttp.Get(source.address(title), &body, source.options);
			if (status == B_ENTRY_NOT_FOUND)
				continue;
			if (status != B_OK) {
				spdlog::info("{} did not answer: {}", source.label, strerror(status));
				break;
			}
			std::vector<Paper> hits;
			if (!source.parse(body, &hits)) {
				// (a page that wants proof of being no bot, an answer that says "too many requests")
				spdlog::info("{} answered with something that is no list of papers", source.label);
				break;
			}
			for (size_t rank = 0; rank < hits.size(); rank++) {
				const Paper& hit = hits[rank];
				const double similarity = TitleSimilarity(title, hit.title);
				const std::string key = !hit.doi.empty() ? hit.doi : (!hit.arxivId.empty() ? hit.arxivId : NormalizeTitle(hit.title));
				if (similarity < kMinTitleSimilarity || !seen.insert(key).second)
					continue;
				spdlog::info("title '{}' matches '{}' at {}", title, hit.title, source.label);
				matches.push_back({hit, ScoreHit(similarity, (int) rank, (int) hits.size(), hit.year)});
			}
			if (!matches.empty())
				break;
		}
		if (matches.empty())
			continue;

		size_t best = 0;
		for (size_t i = 1; i < matches.size(); i++) {
			if (matches[i].score > matches[best].score)
				best = i;
		}
		if (matches.size() > 1 && fSettings.choose) {
			std::vector<sen::Candidate> candidates;
			for (const Match& match : matches) {
				sen::Candidate candidate;
				candidate.title = match.paper.title.c_str();
				if (match.paper.year > 0)
					candidate.subtitle << match.paper.year;
				if (!match.paper.venue.empty())
					candidate.subtitle << (candidate.subtitle.IsEmpty() ? "" : " - ") << match.paper.venue.c_str();
				if (!match.paper.doi.empty())
					candidate.detail << "DOI " << match.paper.doi.c_str();
				else if (!match.paper.arxivId.empty())
					candidate.detail << "arXiv " << match.paper.arxivId.c_str();
				candidate.source = source.label;
				candidate.score = (float) match.score;
				candidates.push_back(candidate);
			}
			const int32 picked = _AskWhich(candidates);
			if (picked < 0 || picked >= (int32) matches.size())
				return B_CANCELED;
			best = (size_t) picked;
		}
		*found = matches[best].paper;
		*how = source::kTitle;
		return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
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
		Reference hit;
		const status_t search = _FindByTitle(facts, &hit);
		if (search == B_CANCELED) {
			// the user did not choose: nothing is written, not even the title of the document
			fSummary = std::string("No paper was chosen for ") + file->name + ": nothing was changed.";
			return B_CANCELED;
		}
		if (search == B_OK && (!hit.doi.empty() || !hit.openAlexId.empty())) {
			Paper attempt;
			status_t status = !hit.doi.empty() ? _LookUp(hit.doi, &attempt, true) : _LookUpById(hit.openAlexId, &attempt);
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

	// The other sources (arXiv by the identifier that the document names, then the title at Semantic Scholar, DBLP and arXiv). What they find is a paper like any
	// other: it gets the semantic type. A cited paper's arXiv identifier from a later page is not the paper of the file.
	if (!found && (fSettings.semanticScholar || fSettings.dblp || fSettings.arxiv)) {
		std::vector<std::string> arxivIds;
		static const std::string kArxivDoiPrefix = "10.48550/arxiv.";
		for (const Candidate& candidate : candidates) {
			if (std::string(candidate.source) != source::kLaterPage && candidate.doi.compare(0, kArxivDoiPrefix.size(), kArxivDoiPrefix) == 0)
				arxivIds.push_back(candidate.doi.substr(kArxivDoiPrefix.size()));
		}
		Paper other;
		const char* how = source::kTitle;
		const status_t search = _FindElsewhere(facts, factsRead, arxivIds, &other, &how);
		if (search == B_CANCELED) {
			fSummary = std::string("No paper was chosen for ") + file->name + ": nothing was changed.";
			return B_CANCELED;
		}
		if (search == B_OK) {
			paper = other;
			doiSource = how;
			found = true;
		}
	}

	BNode node(file);
	if (!found) {
		// no service knows the paper; the document may still say what it is called
		std::string title = factsRead ? _DocumentTitle(facts) : "";
		if (title.empty()) {
			store::WriteNotFound(node, candidates.empty() ? outcome::kDoiNotFound : outcome::kNotInOpenAlex);
			fSummary = std::string(candidates.empty() ? "No DOI and no title that a scientific source knows in " : "No source knows the DOIs of ")
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
		fSummary = "'" + title + "' is not in the sources (OpenAlex and the others): the title is the one of the document" + (renamed ? ", the file is named after it." : ".");
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

	// the authors are people next to the paper (after the rename: their relation is to the file as it is called now)
	int32 authors = 0;
	if (fSettings.authors != "no" && !paper.authors.empty()) {
		AuthorMaker maker(&fHttp, fSettings, overwrite);
		authors = maker.MakeAuthors(*file, paper);
	}

	reply->AddString(sensei::key::kSemanticType, kSemanticType);
	if (!paper.doi.empty())
		reply->AddString(attr::kIdentifier, paper.doi.c_str());
	reply->AddString(attr::kTitle, paper.title.c_str());
	fSummaryTitle = "Paper enriched";
	const char* const kServiceLabels[][2] = {{service::kArxiv, "arXiv"}, {service::kSemanticScholar, "Semantic Scholar"}, {service::kDblp, "DBLP"}};
	std::string found_at;
	for (const auto& label : kServiceLabels) {
		if (paper.service == label[0])
			found_at = std::string(", found at ") + label[1];
	}
	fSummary = "'" + paper.title + "'" + (paper.oaStatus.empty() ? "" : ", open access: " + paper.oaStatus)
		+ (paper.fromOpenAlex ? ", " + std::to_string(references.size()) + " cited works" : found_at)
		+ (authors > 0 ? ", " + std::to_string(authors) + (authors == 1 ? " author" : " authors") : "")
		+ (renamed ? "; the file is named after the title." : ".");
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
