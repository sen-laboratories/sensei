/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// What the plugin writes (MovieWriter.cpp, MoviePeople.cpp) for a movie that TMDB has described, with the answers made up here and no network: the attributes of the file, the
// poster, and the people with their Contribution relations, made in the Library of the current context with a SEN server ("server") and next to the movie without ("noserver").
// people.sh runs it; it removes what it makes. Haiku only.

#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>

#include <stdio.h>
#include <string.h>
#include <string>

#include <sen/Sen.h>
#include <sen/SenLibrary.h>
#include <sen/SenList.h>
#include <sen/SenOntoCore.h>
#include <sen/SenOntoMovies.h>
#include <sen/SenOntoPeople.h>

#include "TestSupport.h"				// (of sen-core: the files of a test, the relations that are stored)
#include "../MoviePeople.h"
#include "../MovieWriter.h"
#include "../../../common/ThumbnailUtil.h"

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

namespace {

// a PNG of one pixel: what a photo is to the translators
const unsigned char kPng[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00,
	0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7, 0x2f, 0x0d,
	0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

/** TMDB, made up: two people, one with a photo */
class FakePeople : public movie::PersonSource {
public:
	int fetched = 0;

	virtual bool Fetch(int64_t id, movie::PersonDetails* person)
	{
		fetched++;
		person->id = id;
		if (id == 90001) {
			person->name = "Test Director Zz";
			person->biography = "Directs.";
			person->birthday = "1965-06-21";
			person->imdbId = "nm0000001";
			person->wikidataId = "Q1";
		} else if (id == 90002) {
			person->name = "Test Actor Zz";
			person->profilePath = "/actor.jpg";
		} else {
			return false;		// (TMDB does not know this one: the credit has the name)
		}
		return true;
	}

	virtual bool FetchPhoto(const std::string& path, std::string* image)
	{
		if (path != "/actor.jpg")
			return false;
		image->assign(reinterpret_cast<const char*>(kPng), sizeof(kPng));
		return true;
	}
};

std::string
Text(const entry_ref& ref, const char* attribute)
{
	BNode node(&ref);
	BString value;
	node.ReadAttrString(attribute, &value);
	return value.String();
}

std::string
PathOf(const entry_ref& ref)
{
	return BPath(&ref).Path();
}

}	// namespace

int main(int argc, char** argv)
{
	BApplication app("application/x-vnd.sen-labs.movie-people-test");
	const bool server = argc > 1 && strcmp(argv[1], "server") == 0;

	Fixture f("movie", 0);
	entry_ref file = f.Create("Test Movie Zz (1999).mkv", "video/x-matroska");

	movie::Details details;
	details.id = 90000;
	details.title = "Test Movie Zz";
	details.originalTitle = "Der Testfilm Zz";
	details.releaseDate = "1999-03-30";
	details.runtime = 136;
	details.overview = "A test.";
	details.genres = {"Action", "Science Fiction"};
	details.languages = {"en", "fr"};
	details.countries = {"US"};
	details.imdbId = "tt0000000";

	// the attributes of the movie
	CHECK(movie::WriteMovie(file, details, true) == B_OK);
	namespace attr = sen::onto::movies::attr;
	CHECK(Text(file, attr::kTitle) == "Test Movie Zz" && Text(file, attr::kOriginalTitle) == "Der Testfilm Zz");
	CHECK(Text(file, attr::kDuration) == "PT2H16M" && Text(file, attr::kDescription) == "A test.");
	CHECK(Text(file, attr::kGenre) == "Action,Science Fiction" && Text(file, attr::kInLanguage) == "en,fr" && Text(file, attr::kCountryOfOrigin) == "US");
	CHECK(Text(file, attr::kSameAs).find("themoviedb.org/movie/90000") != std::string::npos && Text(file, attr::kSameAs).find("imdb.com/title/tt0000000") != std::string::npos);
	CHECK(Text(file, sen::attr::kType) == sen::onto::movies::mime::kMovie);
	BNode node(&file);
	time_t released = 0;
	CHECK(node.ReadAttr(attr::kReleaseDate, B_TIME_TYPE, 0, &released, sizeof(released)) == (ssize_t)sizeof(released) && released == 86400 * 10680);	// 1999-03-30
	char type[B_MIME_TYPE_LENGTH] = "";
	BNodeInfo(&node).GetType(type);
	CHECK(strcmp(type, "video/x-matroska") == 0);			// (the type for Haiku stays)

	// what the file has is kept without overwrite, replaced with it; the links are added to, not replaced
	const std::string mine = "My Title";
	node.WriteAttr(attr::kTitle, B_STRING_TYPE, 0, mine.c_str(), mine.size() + 1);
	node.WriteAttr(attr::kSameAs, B_STRING_TYPE, 0, "https://example.org/mine", 25);
	CHECK(movie::WriteMovie(file, details, false) == B_OK);
	CHECK(Text(file, attr::kTitle) == "My Title");
	CHECK(Text(file, attr::kSameAs).find("https://example.org/mine") == 0 && Text(file, attr::kSameAs).find("themoviedb.org/movie/90000") != std::string::npos);
	CHECK(movie::WriteMovie(file, details, true) == B_OK);
	CHECK(Text(file, attr::kTitle) == "Test Movie Zz");
	// (the same links again are not listed twice)
	CHECK(sen::list::Split(Text(file, attr::kSameAs), ',').size() == 4);

	// the poster is the thumbnail; kept without overwrite
	const std::string image(reinterpret_cast<const char*>(kPng), sizeof(kPng));
	CHECK(movie::WritePoster(file, image, false) == B_OK && HasAttr(file, THUMBNAIL_ATTR_NAME));

	// the names only (the settings say: no entities of their own)
	{
		std::vector<movie::Credit> named;
		movie::Credit one;
		one.name = "Name Director Zz"; one.role = "Director"; one.position = 1;
		movie::Credit two;
		two.name = "Name Actor Zz"; two.role = "Actor"; two.position = 1;
		named = {one, two};
		CHECK(movie::WriteNames(file, named, false) == B_OK);
		CHECK(Text(file, sen::onto::movies::attr::kDirector) == "Name Director Zz" && Text(file, sen::onto::movies::attr::kActor) == "Name Actor Zz");
		named[0].name = "Other Zz";
		CHECK(movie::WriteNames(file, named, false) == B_OK && Text(file, sen::onto::movies::attr::kDirector) == "Name Director Zz");	// (kept)
		CHECK(movie::WriteNames(file, named, true) == B_OK && Text(file, sen::onto::movies::attr::kDirector) == "Other Zz");
		BNode node(&file);
		node.RemoveAttr(sen::onto::movies::attr::kDirector);
		node.RemoveAttr(sen::onto::movies::attr::kActor);
	}

	// the people
	std::vector<movie::Credit> credits;
	movie::Credit director;
	director.personId = 90001; director.name = "Test Director Zz"; director.role = "Director"; director.position = 1;
	movie::Credit writer = director;
	writer.role = "Writer";
	movie::Credit actor;
	actor.personId = 90002; actor.name = "Test Actor Zz"; actor.role = "Actor"; actor.character = "Hero"; actor.position = 1; actor.profilePath = "/actor.jpg";
	movie::Credit unknown;
	unknown.personId = 90003; unknown.name = "Test Extra Zz"; unknown.role = "Actor"; unknown.position = 2;
	credits = {director, writer, actor, unknown};

	FakePeople source;
	movie::PeopleResult first = movie::MakePeople(file, credits, &source, true, false);
	CHECK(first.people == 3 && first.made == 3 && first.skipped == 0);
	CHECK(source.fetched == 3);							// (the person with two credits is asked for once)
	CHECK(first.contributions == (server ? 4 : 0));

	// the person files: in the Library with a server, next to the movie without; the details are the attributes
	BDirectory folder;
	BEntry(&file).GetParent(&folder);
	entry_ref directorFile, actorFile, extraFile;
	auto find = [&](const char* name, entry_ref* ref) {
		entry_ref library;
		if (server && sen::library::FolderFor(sen::onto::people::mime::kPerson, &library) == B_OK) {
			BDirectory libraryFolder(&library);
			BEntry entry(&libraryFolder, name);
			if (entry.Exists())
				return entry.GetRef(ref) == B_OK;
		}
		BEntry entry(&folder, name);
		return entry.Exists() && entry.GetRef(ref) == B_OK;
	};
	CHECK(find("Test Director Zz", &directorFile) && find("Test Actor Zz", &actorFile) && find("Test Extra Zz", &extraFile));
	if (server)
		CHECK(PathOf(directorFile).find("/Library/") != std::string::npos);
	else
		CHECK(PathOf(directorFile).find(f.dir.String()) == 0);
	namespace people = sen::onto::people::attr;
	CHECK(Text(directorFile, people::kName) == "Test Director Zz" && Text(directorFile, people::kDescription) == "Directs.");
	CHECK(Text(directorFile, people::kSameAs).find("themoviedb.org/person/90001") != std::string::npos && Text(directorFile, people::kSameAs).find("wikidata.org/entity/Q1") != std::string::npos);
	BNode directorNode(&directorFile);
	time_t born = 0;
	CHECK(directorNode.ReadAttr(people::kBirthDate, B_TIME_TYPE, 0, &born, sizeof(born)) == (ssize_t)sizeof(born) && born == 86400 * (int64_t)(-1655));	// 1965-06-21
	CHECK(Text(directorFile, people::kPhotoCredit) == "The Movie Database (TMDB)");
	CHECK(Text(directorFile, sen::attr::kType).empty());	// (a person is a file of the type, the type is not a semantic type of its own)
	CHECK(HasAttr(actorFile, THUMBNAIL_ATTR_NAME) && !HasAttr(directorFile, THUMBNAIL_ATTR_NAME));	// (the photo of the actor)
	CHECK(Text(extraFile, people::kName) == "Test Extra Zz");										// (TMDB did not know the person: the name of the credit)

	// the Contributions: a person with two credits has two, with the role (the character and the place for an actor)
	if (server) {
		const std::string movieId = IdOf(file);
		CHECK(!movieId.empty());
		CHECK(Sets(directorFile, sen::onto::core::mime::kContribution, movieId) == 2);
		CHECK(Sets(actorFile, sen::onto::core::mime::kContribution, movieId) == 1);
		CHECK(Sets(file, sen::onto::core::mime::kContribution, IdOf(directorFile)) == 2);		// (the other direction)
		BMessage stored = Stored(actorFile, sen::onto::core::mime::kContribution), set;
		CHECK(stored.FindMessage(movieId.c_str(), &set) == B_OK);
		CHECK(std::string(set.GetString(sen::onto::core::attr::kRelRole, "")) == "Actor" && std::string(set.GetString(sen::onto::core::attr::kRelCharacter, "")) == "Hero");
		CHECK(set.GetInt32(sen::onto::core::attr::kRelPosition, 0) == 1);
		BMessage directorSets = Stored(directorFile, sen::onto::core::mime::kContribution), a, b;
		CHECK(directorSets.FindMessage(movieId.c_str(), 0, &a) == B_OK && directorSets.FindMessage(movieId.c_str(), 1, &b) == B_OK);
		const std::string roles = std::string(a.GetString(sen::onto::core::attr::kRelRole, "")) + "," + b.GetString(sen::onto::core::attr::kRelRole, "");
		CHECK(roles == "Director,Writer" || roles == "Writer,Director");
	}

	// again: nobody is made twice and no Contribution is added twice
	FakePeople again;
	movie::PeopleResult second = movie::MakePeople(file, credits, &again, false, false);
	CHECK(second.people == 3 && second.made == 0);
	if (server) {
		CHECK(Sets(directorFile, sen::onto::core::mime::kContribution, IdOf(file)) == 2);
		CHECK(Sets(actorFile, sen::onto::core::mime::kContribution, IdOf(file)) == 1);
	}

	// TMDB moves a credit (a producer is added before it) and corrects a character: the Contribution is replaced, not added to
	if (server) {
		std::vector<movie::Credit> moved = credits;
		moved[0].position = 2;				// the director
		moved[2].position = 4;				// the actor
		moved[2].character = "Heroine";
		FakePeople third;
		movie::MakePeople(file, moved, &third, false, false);
		CHECK(Sets(directorFile, sen::onto::core::mime::kContribution, IdOf(file)) == 2);	// (Director, Writer: still one for each role)
		CHECK(Sets(actorFile, sen::onto::core::mime::kContribution, IdOf(file)) == 1);
		BMessage stored = Stored(actorFile, sen::onto::core::mime::kContribution), set;
		CHECK(stored.FindMessage(IdOf(file).c_str(), &set) == B_OK);
		CHECK(set.GetInt32(sen::onto::core::attr::kRelPosition, 0) == 4);
		CHECK(std::string(set.GetString(sen::onto::core::attr::kRelCharacter, "")) == "Heroine");
		CHECK(Sets(file, sen::onto::core::mime::kContribution, IdOf(actorFile)) == 1);		// (and in the other direction)
	}

	// remove what was made (the Library itself stays)
	for (const entry_ref& ref : {directorFile, actorFile, extraFile})
		BEntry(&ref).Remove();

	printf("%s: %d checks, %d failure(s)\n", server ? "with the server" : "without", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
