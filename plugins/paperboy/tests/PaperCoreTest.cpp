/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// Tests of core/: no Haiku needed. Build and run with tests/run.sh (needs the nlohmann/json headers).

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "PaperAbstract.h"
#include "PaperApi.h"
#include "PaperDate.h"
#include "PaperDoi.h"
#include "PaperJson.h"
#include "PaperName.h"
#include "PaperSettings.h"
#include "PaperTitle.h"

using namespace paperboy;

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

#define CHECK_EQ(actual, expected) \
	do { \
		sChecks++; \
		std::string a_ = (actual), e_ = (expected); \
		if (a_ != e_) { \
			sFailures++; \
			std::printf("FAIL %s:%d: %s\n   got: '%s'\n   expected: '%s'\n", __FILE__, __LINE__, #actual, a_.c_str(), e_.c_str()); \
		} \
	} while (0)

static std::string sDataDir = "tests/data";

static std::string
ReadFixture(const char* name)
{
	std::ifstream file(sDataDir + "/" + name);
	std::stringstream buffer;
	buffer << file.rdbuf();
	if (buffer.str().empty())
		std::printf("fixture %s/%s is missing\n", sDataDir.c_str(), name);
	return buffer.str();
}


static void
TestDoi()
{
	CHECK_EQ(NormalizeDoi("10.1038/nature14539"), "10.1038/nature14539");
	CHECK_EQ(NormalizeDoi("https://doi.org/10.1016/J.ISPRSJPRS.2023.01.002"), "10.1016/j.isprsjprs.2023.01.002");
	CHECK_EQ(NormalizeDoi("http://dx.doi.org/10.1000/182."), "10.1000/182");
	CHECK_EQ(NormalizeDoi("doi:10.1007/s00134-020-06294-x"), "10.1007/s00134-020-06294-x");
	CHECK_EQ(NormalizeDoi("DOI: 10.1007/s00134-020-06294-x"), "10.1007/s00134-020-06294-x");
	CHECK_EQ(NormalizeDoi("urn:doi:10.1000/182"), "10.1000/182");
	CHECK_EQ(NormalizeDoi("  10.1002/(SICI)1097-4571(199806)49:8<693::AID-ASI4>3.0.CO;2-O "), "10.1002/(sici)1097-4571(199806)49:8");
	CHECK_EQ(NormalizeDoi("not a doi"), "");
	CHECK_EQ(NormalizeDoi("10.12/abc"), "");			// registrant code too short
	CHECK_EQ(NormalizeDoi("10.1234/"), "");				// no suffix
	CHECK_EQ(NormalizeDoi(""), "");

	// in running text
	std::string text =
		"Journal of Things, 2023.\nhttps://doi.org/10.1016/j.x.2023.01.002 0924-2716/(c) 2023.\n"
		"Cited by (see doi:10.1000/182), also 10.1038/nature14539, and again 10.1038/nature14539.\n"
		"Version 3.10.1 of the tool, ISBN 978-3-16-148410-0, page 10.5.";
	std::vector<std::string> dois = FindDois(text);
	CHECK(dois.size() == 3);
	if (dois.size() == 3) {
		CHECK_EQ(dois[0], "10.1016/j.x.2023.01.002");
		CHECK_EQ(dois[1], "10.1000/182");
		CHECK_EQ(dois[2], "10.1038/nature14539");
	}
	CHECK(FindDois("doi10.1016/j.cell.2020.01.001 follows").size() == 1);
	CHECK(FindDois("no numbers here").empty());
	CHECK(FindDois("a 210.1234/abc is no doi").empty());

	// a long text must not overflow anything
	std::string big(2000000, 'x');
	big += " 10.1038/nature14539";
	CHECK(FindDois(big).size() == 1);

	// arXiv
	std::vector<std::string> ids = FindArxivIds("Preprint. arXiv:1706.03762v5 [cs.CL] 2 Aug 2023");
	CHECK(ids.size() == 1 && ids[0] == "1706.03762");
	ids = FindArxivIds("see https://arxiv.org/abs/2401.12345 and arXiv:hep-th/9901001v2");
	CHECK(ids.size() == 2);
	if (ids.size() == 2) {
		CHECK_EQ(ids[0], "2401.12345");
		CHECK_EQ(ids[1], "hep-th/9901001");
	}
	CHECK(FindArxivIds("arXiv: soon").empty());
	CHECK_EQ(ArxivDoi("1706.03762"), "10.48550/arxiv.1706.03762");
	CHECK_EQ(ArxivDoi(""), "");
}


static void
TestDate()
{
	CHECK(ParseDate("2015-05-26") == 1432598400);
	CHECK(ParseDate("2015-05") == 1430438400);
	CHECK(ParseDate("2015") == 1420070400);
	CHECK(ParseDate("2024-02-29") == 1709164800);
	CHECK(ParseDate("2023-02-29") == -1);
	CHECK(ParseDate("2015-13-01") == -1);
	CHECK(ParseDate("26.05.2015") == -1);
	CHECK(ParseDate("2015-05-26T10:00:00") == -1);
	CHECK(ParseDate("") == -1);
	CHECK(ParseDate("abcd") == -1);
}


static void
TestAbstract()
{
	// "The dominant models are dominant" with a gap at position 4
	std::vector<InvertedWord> index = {
		{"The", {0}}, {"dominant", {1, 5}}, {"models", {2}}, {"are", {3}}, {"end", {6}}
	};
	CHECK_EQ(ReconstructAbstract(index), "The dominant models are dominant end");
	CHECK_EQ(ReconstructAbstract({}), "");
	CHECK_EQ(ReconstructAbstract({{"x", {}}}), "");
	// nonsense positions are ignored
	CHECK_EQ(ReconstructAbstract({{"a", {0}}, {"b", {999999999}}, {"c", {-3}}}), "a");
}


static void
TestJson()
{
	// OpenAlex, a closed journal paper without abstract but with a green copy
	Paper nature;
	CHECK(ParseOpenAlexWork(ReadFixture("openalex-nature.json"), &nature));
	CHECK_EQ(nature.doi, "10.1038/nature14539");
	CHECK_EQ(nature.openAlexId, "W2919115771");
	CHECK_EQ(nature.title, "Deep learning");
	CHECK_EQ(nature.date, "2015-05-26");
	CHECK(nature.year == 2015);
	CHECK_EQ(nature.type, "article");
	CHECK_EQ(nature.language, "en");
	CHECK_EQ(nature.venue, "Nature");
	CHECK(nature.abstract.empty());
	CHECK(nature.authors.size() == 3);
	if (!nature.authors.empty()) {
		CHECK_EQ(nature.authors[0].name, "Yann LeCun");
		CHECK_EQ(nature.authors[0].orcid, "0000-0002-1992-2684");
		CHECK(!nature.authors[0].affiliations.empty());
	}
	CHECK(!nature.keywords.empty());
	CHECK_EQ(nature.keywords.empty() ? "" : nature.keywords[0], "deep learning");
	CHECK(nature.citedByCount > 1000);
	CHECK(nature.referencedWorks.size() == 5);
	CHECK(nature.referencedWorks.empty() || nature.referencedWorks[0].rfind("W", 0) == 0);
	CHECK(nature.isOa);
	CHECK_EQ(nature.oaStatus, "green");
	CHECK(nature.BestLocation() != nullptr);
	if (nature.BestLocation()) {
		CHECK_EQ(nature.BestLocation()->landingUrl, "https://hal.science/hal-04206682");
		CHECK(nature.BestLocation()->isBest);
	}

	// Unpaywall replaces the locations and keeps what OpenAlex said about the work
	CHECK(ParseUnpaywall(ReadFixture("unpaywall-nature.json"), &nature));
	CHECK(nature.fromUnpaywall && nature.fromOpenAlex);
	CHECK(nature.isOa);
	CHECK_EQ(nature.oaStatus, "green");
	CHECK_EQ(nature.title, "Deep learning");
	CHECK(nature.BestLocation() != nullptr);
	if (nature.BestLocation()) {
		CHECK_EQ(nature.BestLocation()->source, "unpaywall");
		CHECK_EQ(nature.BestLocation()->version, "submittedVersion");
		CHECK_EQ(nature.BestLocation()->Url(), "https://hal.science/hal-04206682");
	}

	// OpenAlex, a preprint with abstract and a direct PDF link
	Paper preprint;
	CHECK(ParseOpenAlexWork(ReadFixture("openalex-preprint.json"), &preprint));
	CHECK_EQ(preprint.type, "preprint");
	CHECK(preprint.abstract.rfind("The dominant", 0) == 0);
	CHECK(preprint.abstract.size() > 500);
	CHECK(preprint.keywords.size() == 7);
	CHECK(preprint.isOa);
	CHECK_EQ(preprint.oaStatus, "gold");
	CHECK(preprint.BestLocation() != nullptr);
	if (preprint.BestLocation())
		CHECK_EQ(preprint.BestLocation()->pdfUrl, "https://langtaosha.org.cn/index.php/lts/preprint/download/10/108");

	// not JSON, not a work, not a DOI record
	Paper none;
	CHECK(!ParseOpenAlexWork("<!doctype html><title>404 Not Found</title>", &none));
	CHECK(!ParseOpenAlexWork("{\"error\": \"Not found\"}", &none));
	CHECK(!ParseOpenAlexWork("", &none));
	CHECK(!ParseUnpaywall("<!doctype html>", &none));
	CHECK(!ParseUnpaywall("{\"HTTP_status_code\": 404}", &none));
	// null fields do not throw
	CHECK(ParseOpenAlexWork("{\"id\":\"https://openalex.org/W1\",\"title\":null,\"doi\":null,\"authorships\":[{\"author\":null}],"
		"\"keywords\":null,\"abstract_inverted_index\":null,\"open_access\":null,\"best_oa_location\":null,\"locations\":null}", &none));
	CHECK(none.title.empty() && none.authors.empty() && !none.isOa);

	// lists: references and title search
	std::vector<Reference> batch = ParseOpenAlexList(ReadFixture("openalex-batch.json"));
	CHECK(batch.size() == 2);
	if (batch.size() == 2) {
		CHECK_EQ(batch[0].openAlexId, "W146900863");
		CHECK_EQ(batch[0].doi, "10.7551/mitpress/11474.003.0014");
		CHECK(batch[1].doi.empty());
		CHECK(batch[1].year == 2001);
	}
	std::vector<Reference> hits = ParseOpenAlexList(ReadFixture("openalex-search.json"));
	CHECK(hits.size() == 3);
	CHECK(hits.empty() || TitleSimilarity(hits[0].title, "Attention is all you need") > 0.99);
	CHECK(hits.size() < 2 || TitleSimilarity(hits[1].title, "Attention is all you need") < 0.8);
	CHECK(ParseOpenAlexList("garbage").empty());

	CHECK_EQ(ShortOpenAlexId("https://openalex.org/W42"), "W42");
	CHECK_EQ(ShortOpenAlexId("W42"), "W42");
}


static void
TestNames()
{
	CHECK_EQ(FamilyName("Yann LeCun"), "LeCun");
	CHECK_EQ(FamilyName("Doe, John"), "Doe");
	CHECK_EQ(FamilyName("J. R. R. Tolkien"), "Tolkien");
	CHECK_EQ(FamilyName("Plato"), "Plato");
	CHECK_EQ(FamilyName(""), "");

	// blanks stay, what is illegal or unloved goes
	CHECK_EQ(SanitizeForFileName("Semantic Integration of Biodiversity Data"), "Semantic Integration of Biodiversity Data");
	CHECK_EQ(SanitizeForFileName("FS2KG: From File Systems to Knowledge Graphs (Demo)"), "FS2KG - From File Systems to Knowledge Graphs (Demo)");
	CHECK_EQ(SanitizeForFileName("Is \"AI\" a fad? A/B <test> | or *not*"), "Is AI a fad A-B test or not");
	CHECK_EQ(SanitizeForFileName("  a   b \\ c  "), "a b c");
	CHECK_EQ(SanitizeForFileName("..hidden: -name."), "hidden - -name");
	CHECK_EQ(SanitizeForFileName("Über größere Räume"), "Über größere Räume");
	CHECK_EQ(SanitizeForFileName("Cats, dogs & it's 100% fine; really"), "Cats, dogs & it's 100% fine; really");
	CHECK_EQ(SanitizeForFileName("tab\tand\nnewline\x01here"), "tab and newlinehere");
	// with underscores
	CHECK_EQ(SanitizeForFileName("Semantic Integration: of \"Biodiversity\" Data?", 100, true), "Semantic_Integration_of_Biodiversity_Data");
	CHECK_EQ(SanitizeForFileName("  a / b \\ c  ", 100, true), "a_-_b_c");
	// cut at a word boundary, and never inside a UTF-8 sequence
	std::string cut = SanitizeForFileName("one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen", 30);
	CHECK(cut.size() <= 30);
	CHECK_EQ(cut, "one two three four five six");
	std::string umlauts = SanitizeForFileName("ääääääääääääääääääääää", 11);
	CHECK(umlauts.size() <= 11 && umlauts.size() % 2 == 0);
	CHECK(SanitizeForFileName(std::string(300, 'x')).size() <= 100);

	Paper paper;
	paper.title = "Semantic Integration of Biodiversity Data";
	paper.year = 2025;
	paper.doi = "10.1000/182";
	Author author;
	author.name = "Jane Doe";
	paper.authors.push_back(author);
	// the default: the title
	CHECK_EQ(BuildFileName(kDefaultNamePattern, paper, ".pdf"), "Semantic Integration of Biodiversity Data.pdf");
	CHECK_EQ(BuildFileName("", paper, ".epub"), "Semantic Integration of Biodiversity Data.epub");
	CHECK_EQ(BuildFileName("{year}_{first_author}_{title}", paper, ".pdf", true), "2025_Doe_Semantic_Integration_of_Biodiversity_Data.pdf");
	CHECK_EQ(BuildFileName("{year}_{first_author}_{title}", paper, ".pdf"), "2025_Doe_Semantic Integration of Biodiversity Data.pdf");
	CHECK_EQ(BuildFileName("{doi}", paper, ".pdf"), "10.1000_182.pdf");
	paper.authors.clear();
	CHECK_EQ(BuildFileName("{year}_{first_author}_{title}", paper, ".pdf", true), "2025_Semantic_Integration_of_Biodiversity_Data.pdf");
	paper.title = "A: B/C";
	CHECK_EQ(BuildFileName(kDefaultNamePattern, paper, ""), "A - B-C");
	paper.title.clear();
	CHECK_EQ(BuildFileName(kDefaultNamePattern, paper, ".pdf"), "");
	paper.title = "???";
	CHECK_EQ(BuildFileName(kDefaultNamePattern, paper, ".pdf"), "");
}


static void
TestSettings()
{
	Settings settings;
	CHECK(settings.unpaywall && settings.rename && !settings.overwrite && settings.maxPages == 3);
	CHECK_EQ(settings.renamePattern, "{title}");
	CHECK(!settings.renameUnderscores && settings.apiKey.empty());

	ParseSettings("# paperboy\nemail = reader@example.org   # for the polite pool\nrename = yes\nrename_pattern = {year}-{title}\n"
		"unpaywall = off\nmax_pages=5\nlocal_only = true\nrename_underscores = yes\napi_key = abc123\nbogus line\nunknown = 1\n", &settings);
	CHECK_EQ(settings.email, "reader@example.org");
	CHECK(settings.rename);
	CHECK(settings.renameUnderscores);
	CHECK_EQ(settings.apiKey, "abc123");
	CHECK_EQ(settings.renamePattern, "{year}-{title}");
	CHECK(!settings.unpaywall);
	CHECK(settings.maxPages == 5);
	CHECK(settings.localOnly);

	ParseSettings("email = not an address\nmax_pages = 500\nrename = maybe\napi_key = has space\n", &settings);
	CHECK_EQ(settings.apiKey, "abc123");
	CHECK_EQ(settings.email, "reader@example.org");
	CHECK(settings.maxPages == 5);
	CHECK(settings.rename);

	CHECK(LooksLikeEmail("a@b.org"));
	CHECK(!LooksLikeEmail("a@b"));
	CHECK(!LooksLikeEmail("@b.org"));
	CHECK(!LooksLikeEmail("a b@c.org"));
	CHECK(ParseBool("YES", false) && !ParseBool("No", true) && ParseBool("?", true));
}


static void
TestTitleAndApi()
{
	CHECK_EQ(NormalizeTitle("  Deep Learning: A Review!  "), "deep learning a review");
	CHECK(TitleSimilarity("Deep learning", "DEEP LEARNING.") > 0.99);
	CHECK(TitleSimilarity("Deep learning", "Shallow thinking") == 0);
	CHECK(TitleSimilarity("", "x") == 0);

	CHECK(LooksLikeTitle("Semantic Integration of Biodiversity Data"));
	CHECK(!LooksLikeTitle("Microsoft Word - draft_v3.docx"));
	CHECK(!LooksLikeTitle("paper.pdf"));
	CHECK(!LooksLikeTitle("Untitled"));
	CHECK(!LooksLikeTitle("Slide 1"));
	CHECK(!LooksLikeTitle("Introduction"));
	CHECK(!LooksLikeTitle("final version of the draft.tex"));
	CHECK(!LooksLikeTitle(""));

	CHECK_EQ(UrlEncode("a b&c/d"), "a%20b%26c%2Fd");
	CHECK_EQ(UrlEncode("a b/c", "/"), "a%20b/c");
	CHECK_EQ(OpenAlexWorkUrl("10.1038/nature14539", {}), "https://api.openalex.org/works/https://doi.org/10.1038/nature14539");
	CHECK_EQ(OpenAlexWorkUrl("10.1002/(sici)1097:8", {"me@example.org", ""}),
		"https://api.openalex.org/works/https://doi.org/10.1002/(sici)1097:8?mailto=me%40example.org");
	CHECK_EQ(OpenAlexSearchUrl("Deep, learning | a: review", {}),
		"https://api.openalex.org/works?search=Deep%20%20learning%20%20%20a%20%20review&per-page=5&select=id,doi,title,publication_year");
	CHECK_EQ(OpenAlexBatchUrl({"W1", "W2"}, {}),
		"https://api.openalex.org/works?filter=openalex:W1%7CW2&per-page=2&select=id,doi,title,publication_year");
	std::vector<std::string> many;
	for (int i = 0; i < 80; i++)
		many.push_back("W" + std::to_string(i));
	CHECK(OpenAlexBatchUrl(many, {}).find("per-page=50") != std::string::npos);
	CHECK_EQ(OpenAlexWorkUrl("10.1/abc", {"me@example.org", "k3y"}),
		"https://api.openalex.org/works/https://doi.org/10.1/abc?mailto=me%40example.org&api_key=k3y");
	CHECK_EQ(OpenAlexSearchUrl("x", {"", "k3y"}), "https://api.openalex.org/works?search=x&per-page=5&select=id,doi,title,publication_year&api_key=k3y");
	CHECK_EQ(UnpaywallUrl("10.1038/nature14539", "me@example.org"), "https://api.unpaywall.org/v2/10.1038/nature14539?email=me%40example.org");
	CHECK_EQ(UserAgent(""), "paperboy/0.1 (SEN Labs; sen-labs.org)");
	CHECK_EQ(UserAgent("me@example.org"), "paperboy/0.1 (mailto:me@example.org)");
}


int
main(int argc, char** argv)
{
	if (argc > 1)
		sDataDir = argv[1];

	TestDoi();
	TestDate();
	TestAbstract();
	TestJson();
	TestNames();
	TestSettings();
	TestTitleAndApi();

	std::printf("%d checks, %d failed\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
