/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperSources.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "PaperApi.h"
#include "PaperDoi.h"
#include "PaperString.h"

namespace paperboy {

namespace {

using nlohmann::json;

const char* const kSemanticScholarFields = "title,year,venue,publicationDate,externalIds,authors,abstract,openAccessPdf,isOpenAccess,citationCount,publicationTypes";

std::string
Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
	return s;
}

// ---- XML (the little that the feed of arXiv needs)

/** where the element `name` starts in text (at "<name>" or "<name ", from `from`); npos if there is none */
size_t
FindOpen(const std::string& text, const std::string& name, size_t from)
{
	const std::string open = "<" + name;
	for (size_t at = text.find(open, from); at != std::string::npos; at = text.find(open, at + 1)) {
		const size_t after = at + open.size();
		if (after < text.size() && (text[after] == '>' || text[after] == ' ' || text[after] == '/' || text[after] == '\n' || text[after] == '\t'))
			return at;
	}
	return std::string::npos;
}

/** the text inside the first element `name` (as it is, with its entities); false if there is none */
bool
Element(const std::string& text, const std::string& name, std::string* inner, size_t from = 0, size_t* next = nullptr)
{
	const size_t at = FindOpen(text, name, from);
	if (at == std::string::npos)
		return false;
	const size_t tagEnd = text.find('>', at);
	if (tagEnd == std::string::npos)
		return false;
	if (text[tagEnd - 1] == '/') {		// <name .../>
		if (inner != nullptr)
			inner->clear();
		if (next != nullptr)
			*next = tagEnd + 1;
		return true;
	}
	const size_t close = text.find("</" + name + ">", tagEnd);
	if (close == std::string::npos)
		return false;
	if (inner != nullptr)
		*inner = text.substr(tagEnd + 1, close - tagEnd - 1);
	if (next != nullptr)
		*next = close + name.size() + 3;
	return true;
}

/** the value of an attribute in the start tag at `at` */
std::string
Attribute(const std::string& text, size_t at, const char* name)
{
	const size_t end = text.find('>', at);
	const std::string tag = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
	const std::string key = std::string(" ") + name + "=\"";
	const size_t start = tag.find(key);
	if (start == std::string::npos)
		return "";
	const size_t valueStart = start + key.size();
	const size_t valueEnd = tag.find('"', valueStart);
	return valueEnd == std::string::npos ? "" : DecodeEntities(tag.substr(valueStart, valueEnd - valueStart));
}

/** `http://arxiv.org/abs/1706.03762v7` -> `1706.03762`; `.../abs/hep-th/9901001v1` -> `hep-th/9901001` */
std::string
ArxivIdOfUrl(const std::string& url)
{
	const size_t abs = url.find("/abs/");
	if (abs == std::string::npos)
		return "";
	std::string id = url.substr(abs + 5);
	const size_t version = id.rfind('v');
	if (version != std::string::npos && version + 1 < id.size()
			&& std::all_of(id.begin() + version + 1, id.end(), [](unsigned char c) { return std::isdigit(c); }))
		id.erase(version);
	return id;
}

// ---- JSON

std::string
Str(const json& object, const char* key)
{
	if (!object.is_object())
		return "";
	auto it = object.find(key);
	return (it != object.end() && it->is_string()) ? it->get<std::string>() : "";
}

const json&
Child(const json& object, const char* key)
{
	static const json kNull;
	if (!object.is_object())
		return kNull;
	auto it = object.find(key);
	return it != object.end() ? *it : kNull;
}

int
Year(const std::string& text)
{
	return text.size() >= 4 && std::all_of(text.begin(), text.begin() + 4, [](unsigned char c) { return std::isdigit(c); })
		? std::atoi(text.substr(0, 4).c_str()) : 0;
}

}	// namespace


// ---- addresses

std::string
ArxivIdUrl(const std::string& arxivId)
{
	return "https://export.arxiv.org/api/query?id_list=" + UrlEncode(arxivId, "/") + "&max_results=1";
}


std::string
ArxivSearchUrl(const std::string& title)
{
	std::string clean;
	for (char c : title)
		clean += (c == '"' || c == '\n' || c == '\r') ? ' ' : c;
	return "https://export.arxiv.org/api/query?search_query=" + UrlEncode("ti:\"" + clean + "\"") + "&max_results=5";
}


std::string
SemanticScholarSearchUrl(const std::string& title)
{
	return "https://api.semanticscholar.org/graph/v1/paper/search?query=" + UrlEncode(title) + "&limit=5&fields=" + UrlEncode(kSemanticScholarFields, ",");
}


std::string
DblpSearchUrl(const std::string& title)
{
	return "https://dblp.org/search/publ/api?q=" + UrlEncode(title) + "&format=json&h=5";
}


// ---- arXiv

bool
ParseArxivFeed(const std::string& xml, std::vector<Paper>* papers)
{
	if (xml.find("<feed") == std::string::npos)
		return false;

	size_t from = 0, next = 0;
	std::string entry;
	while (Element(xml, "entry", &entry, from, &next)) {
		from = next;

		std::string idText;
		Element(entry, "id", &idText);
		const std::string arxivId = ArxivIdOfUrl(DecodeEntities(idText));
		if (arxivId.empty())
			continue;		// (an error is an entry, too: its id is no paper)

		Paper paper;
		paper.service = service::kArxiv;
		paper.arxivId = arxivId;
		std::string text;
		if (Element(entry, "title", &text))
			paper.title = SqueezeBlanks(DecodeEntities(text));
		if (Element(entry, "summary", &text))
			paper.abstract = SqueezeBlanks(DecodeEntities(text));
		if (Element(entry, "published", &text) && text.size() >= 10) {
			paper.date = text.substr(0, 10);
			paper.year = Year(text);
		}
		paper.type = "preprint";
		paper.publisher = "arXiv";
		paper.language = "en";

		std::string doi;
		if (Element(entry, "arxiv:doi", &doi))
			paper.doi = NormalizeDoi(SqueezeBlanks(DecodeEntities(doi)));
		if (paper.doi.empty())
			paper.doi = ArxivDoi(arxivId);
		if (Element(entry, "arxiv:journal_ref", &text))
			paper.venue = SqueezeBlanks(DecodeEntities(text));
		if (paper.venue.empty())
			paper.venue = "arXiv";

		size_t authorFrom = 0, authorNext = 0;
		std::string author;
		while (Element(entry, "author", &author, authorFrom, &authorNext)) {
			authorFrom = authorNext;
			Author person;
			if (Element(author, "name", &text))
				person.name = SqueezeBlanks(DecodeEntities(text));
			size_t affiliationFrom = 0, affiliationNext = 0;
			while (Element(author, "arxiv:affiliation", &text, affiliationFrom, &affiliationNext)) {
				affiliationFrom = affiliationNext;
				person.affiliations.push_back(SqueezeBlanks(DecodeEntities(text)));
			}
			if (!person.name.empty())
				paper.authors.push_back(person);
		}

		for (size_t at = FindOpen(entry, "category", 0); at != std::string::npos; at = FindOpen(entry, "category", at + 1)) {
			const std::string term = Attribute(entry, at, "term");
			if (!term.empty() && std::find(paper.keywords.begin(), paper.keywords.end(), term) == paper.keywords.end())
				paper.keywords.push_back(term);
		}

		// the preprint is free to read: that is what arXiv is
		Location location;
		location.landingUrl = "https://arxiv.org/abs/" + arxivId;
		for (size_t at = FindOpen(entry, "link", 0); at != std::string::npos; at = FindOpen(entry, "link", at + 1)) {
			if (Attribute(entry, at, "title") == "pdf")
				location.pdfUrl = Attribute(entry, at, "href");
		}
		if (location.pdfUrl.empty())
			location.pdfUrl = "https://arxiv.org/pdf/" + arxivId;
		location.version = "submittedVersion";
		location.hostType = "repository";
		location.source = service::kArxiv;
		location.isBest = true;
		paper.locations.push_back(location);
		paper.isOa = true;
		paper.oaStatus = "green";

		papers->push_back(paper);
	}
	return true;
}


// ---- Semantic Scholar

bool
ParseSemanticScholarSearch(const std::string& body, std::vector<Paper>* papers)
{
	json answer = json::parse(body, nullptr, false);
	const json& data = Child(answer, "data");
	if (!answer.is_object() || !data.is_array())
		return false;

	for (const json& work : data) {
		Paper paper;
		paper.service = service::kSemanticScholar;
		paper.title = SqueezeBlanks(Str(work, "title"));
		if (paper.title.empty())
			continue;
		paper.date = Str(work, "publicationDate");
		paper.year = (int) (Child(work, "year").is_number() ? Child(work, "year").get<double>() : 0);
		if (paper.date.empty() && paper.year > 0)
			paper.date = std::to_string(paper.year);
		paper.venue = Str(work, "venue");
		paper.abstract = SqueezeBlanks(Str(work, "abstract"));

		const json& ids = Child(work, "externalIds");
		paper.doi = NormalizeDoi(Str(ids, "DOI"));
		paper.arxivId = Str(ids, "ArXiv");
		if (paper.doi.empty() && !paper.arxivId.empty())
			paper.doi = ArxivDoi(paper.arxivId);

		const json& kinds = Child(work, "publicationTypes");
		paper.type = "article";
		if (kinds.is_array() && !kinds.empty() && kinds.front().is_string()) {
			const std::string kind = kinds.front().get<std::string>();
			if (kind == "Conference")
				paper.type = "proceedings-article";
			else if (kind == "Book" || kind == "BookSection")
				paper.type = Lower(kind) == "book" ? "book" : "book-chapter";
			else if (kind != "JournalArticle")
				paper.type = Lower(kind);
		}

		const json& authors = Child(work, "authors");
		if (authors.is_array()) {
			for (const json& entry : authors) {
				Author person;
				person.name = Str(entry, "name");
				if (!person.name.empty())
					paper.authors.push_back(person);
			}
		}

		if (Child(work, "citationCount").is_number())
			paper.citedByCount = (int) Child(work, "citationCount").get<double>();

		const json& pdf = Child(work, "openAccessPdf");
		const std::string pdfUrl = Str(pdf, "url");
		if (!pdfUrl.empty()) {
			Location location;
			location.pdfUrl = pdfUrl;
			location.landingUrl = pdfUrl;
			location.license = Str(pdf, "license");
			location.hostType = "repository";
			location.source = service::kSemanticScholar;
			location.isBest = true;
			paper.locations.push_back(location);
			paper.isOa = true;
			paper.oaStatus = Lower(Str(pdf, "status"));
			if (paper.oaStatus.empty())
				paper.oaStatus = "green";
		} else {
			paper.isOa = false;
			paper.oaStatus = "closed";
		}
		papers->push_back(paper);
	}
	return true;
}


// ---- DBLP

namespace {

/** "a" or ["a", "b"] */
std::vector<std::string>
Strings(const json& value)
{
	std::vector<std::string> out;
	if (value.is_string())
		out.push_back(value.get<std::string>());
	else if (value.is_array()) {
		for (const json& item : value) {
			if (item.is_string())
				out.push_back(item.get<std::string>());
		}
	}
	return out;
}

/** DBLP tells people of the same name apart with a number: "Wei Wang 0001" */
std::string
WithoutDblpNumber(std::string name)
{
	const size_t blank = name.rfind(' ');
	if (blank != std::string::npos && name.size() - blank == 5
			&& std::all_of(name.begin() + blank + 1, name.end(), [](unsigned char c) { return std::isdigit(c); }))
		name.erase(blank);
	return name;
}

}	// namespace


bool
ParseDblpSearch(const std::string& body, std::vector<Paper>* papers)
{
	json answer = json::parse(body, nullptr, false);
	const json& hits = Child(Child(answer, "result"), "hits");
	if (!answer.is_object() || !hits.is_object())
		return false;

	const json& list = Child(hits, "hit");
	if (!list.is_array())
		return true;		// (no hit at all: the answer has no list)

	for (const json& hit : list) {
		const json& info = Child(hit, "info");
		Paper paper;
		paper.service = service::kDblp;
		paper.title = SqueezeBlanks(Str(info, "title"));
		if (!paper.title.empty() && paper.title.back() == '.')
			paper.title.pop_back();
		if (paper.title.empty())
			continue;

		paper.year = Year(Str(info, "year"));
		if (paper.year > 0)
			paper.date = std::to_string(paper.year);
		paper.doi = NormalizeDoi(Str(info, "doi"));
		paper.language = "en";

		std::string venue;
		for (const std::string& part : Strings(Child(info, "venue")))
			venue += (venue.empty() ? "" : ", ") + part;
		paper.venue = venue;

		const std::string kind = Str(info, "type");
		if (kind == "Journal Articles")
			paper.type = "article";
		else if (kind == "Conference and Workshop Papers")
			paper.type = "proceedings-article";
		else if (kind == "Informal and Other Publications")
			paper.type = "preprint";
		else if (kind == "Parts in Books or Collections")
			paper.type = "book-chapter";
		else if (kind == "Books and Theses")
			paper.type = "book";
		else
			paper.type = Lower(kind);

		const json& authors = Child(Child(info, "authors"), "author");
		std::vector<json> people;
		if (authors.is_array())
			people.assign(authors.begin(), authors.end());
		else if (authors.is_object() || authors.is_string())
			people.push_back(authors);
		for (const json& entry : people) {
			Author person;
			person.name = WithoutDblpNumber(entry.is_string() ? entry.get<std::string>() : Str(entry, "text"));
			if (!person.name.empty())
				paper.authors.push_back(person);
		}

		// the electronic edition; DBLP says whether it is free
		const std::string link = Str(info, "ee");
		if (!link.empty()) {
			Location location;
			location.landingUrl = link;
			location.hostType = "publisher";
			location.source = service::kDblp;
			location.isBest = true;
			const bool open = Str(info, "access") == "open";
			if (open)
				location.pdfUrl = link.size() > 4 && Lower(link.substr(link.size() - 4)) == ".pdf" ? link : "";
			paper.locations.push_back(location);
			paper.isOa = open;
			paper.oaStatus = open ? "" : "closed";
		}
		papers->push_back(paper);
	}
	return true;
}

}	// namespace paperboy
