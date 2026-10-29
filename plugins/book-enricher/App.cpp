/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */
#include <Alert.h>
#include <Entry.h>
#include <Errors.h>
#include <StringList.h>
#include <fs_attr.h>
#include <NodeInfo.h>
#include <MimeType.h>
#include <Path.h>

#include <math.h>
#include <string.h>

#include <iostream>
#include <vector>

#include "App.h"
#include "BookRanking.h"
#include "../../common/ThumbnailUtil.h"
#include <sen/SenAbout.h>
#include <sen/SenChoice.h>
#include <sen/Sen.h>
#include <sen/Sensei.h>

const char* kApplicationSignature = "application/x-vnd.sen-plugin.sen-labs.book-enricher";

// the most books that are asked for: the user chooses from these
static const char* const kMaxResults = "20";

App::App()
    :
    BApplication(kApplicationSignature),
    fDebugMode(false),
    fOverwrite(true),
    fAuto(false),
    fExitCode(0),
    fBaseEnricher(NULL),
    fMapper(new MappingUtil())
{
}

App::~App()
{
    delete fBaseEnricher;
    delete fMapper;
}

int main()
{
	App* app = new App();
    if (app->InitCheck() != B_OK) {
        delete app;
        return 1;
    }

	app->Run();

    int32 exitCode = app->ExitCode();
    delete app;
	return exitCode;
}

void App::ArgvReceived(int32 argc, char ** argv) {
    // sanity checks
    if (argc < 2) {
        PrintUsage();
        return;
    }

    // real argument parsing
    bool debug = false;
    bool wipe = false;
    bool automatic = false;
    BString inputPath;
    BString outputPath;

    for (int32 argIndex = 1; argIndex < argc; argIndex++) {   // last argument is always input file
        const char* arg = argv[argIndex];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            PrintUsage();
            return;
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--debug") == 0) {
            debug = true;
        } else if (strcmp(arg, "-w") == 0 || strcmp(arg, "--wipe") == 0) {
            wipe = true;
        } else if (strcmp(arg, "-a") == 0 || strcmp(arg, "--auto") == 0) {
            automatic = true;
        } else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
            if (argIndex + 1 >= argc) {
                PrintUsage("The option -o needs a file name.");
                return;
            }
            outputPath = argv[++argIndex];  // the next argument is its value
        } else if (arg[0] == '-') {
            BString errorMsg("unknown parameter ");
            errorMsg.Append(arg);
            PrintUsage(errorMsg.String());
            return;
        } else {
            inputPath = arg;
        }
    }

    if (inputPath.IsEmpty()) {
        PrintUsage("Missing input file." );
        return;
    }

    BMessage refsMsg(B_REFS_RECEIVED);
    BEntry inputEntry(inputPath.String());
    entry_ref ref;

    status_t result = inputEntry.InitCheck();
    if (result == B_OK)
        result = inputEntry.GetRef(&ref);
    if (result != B_OK) {
        BString errorMsg("cannot open ");
        errorMsg << inputPath << ": " << strerror(result);
        PrintUsage(errorMsg.String());
        return;
    }
    refsMsg.AddRef("refs", &ref);

    if (!outputPath.IsEmpty()) {
        BEntry outputEntry(outputPath.String());
        entry_ref outputRef;

        result = outputEntry.InitCheck();
        if (result == B_OK)
            result = outputEntry.GetRef(&outputRef);
        if (result != B_OK) {
            BString errorMsg("cannot use ");
            errorMsg << outputPath << " for the result: " << strerror(result);
            PrintUsage(errorMsg.String());
            return;
        }
        refsMsg.AddRef("outRefs", &outputRef);
    }

    if (debug) {
        refsMsg.AddBool("debug", true);
    }
    if (wipe) {
        refsMsg.AddBool("wipe", true);
    }
    if (automatic) {
        refsMsg.AddBool("auto", true);
    }

    RefsReceived(&refsMsg);
}

void App::ShowError(const char* message)
{
    BAlert* alert = new BAlert("Error in SEN Book Enricher", message, "Oh no.");
    alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
    alert->Go();
}

void App::RefsReceived(BMessage *message)
{
    BMessage reply(sensei::cmd::kResult);
    status_t result = Enrich(message, &reply);

    if (result == B_CANCELED)
        printf("cancelled, nothing was changed.\n");
    else if (result != B_OK)
        fExitCode = 1;

    reply.AddInt32(sensei::key::kResult, result);
    reply.AddString(sen::key::kDetail, strerror(result));

    printf("reply message:\n");
    reply.PrintToStream();

    // we don't expect a reply but run into a race condition with the app
    // being deleted too early, resulting in a malloc assertion failure.
    message->SendReply(&reply, this);

    Quit();
}

/**
 * The work of the plugin: look the book up, write what was found to the attributes of the file, fetch the cover as the thumbnail, and
 * make the author (as far as OpenLibrary knows it). The result is that of the BOOK: that the author could not be found does not undo it.
 */
status_t App::Enrich(BMessage *message, BMessage *reply)
{
    entry_ref ref;

    if (message->FindRef("refs", &ref) != B_OK) {
        ShowError("Failed to resolve input file.");
        return B_BAD_VALUE;
    }

    fDebugMode = message->GetBool("debug", false);
    fOverwrite = message->GetBool("wipe", true);
    fAuto = message->GetBool("auto", false);

    delete fBaseEnricher;
    fBaseEnricher = new BaseEnricher(&ref, fMapper);

    // set up global mapping table (all Strings because it's only about names, not values!)
    fMapper->AddAlias(sen::onto::books::attr::kIsbn, "isbn");
    fMapper->AddAlias(sen::onto::books::attr::kCreator, "author_name");
    fMapper->AddAlias(sen::onto::books::attr::kLanguage, "language");
    fMapper->AddAlias(sen::onto::books::attr::kPublisher, "publisher");
    fMapper->AddAlias(sen::onto::books::attr::kBookFormat, "format");
    fMapper->AddAlias(sen::onto::books::attr::kSubject, "subject");
    fMapper->AddAlias(sen::onto::books::attr::kBookClass, "lcc");
    fMapper->AddAlias(sen::onto::books::attr::kNumberOfPages, "number_of_pages_median");
    fMapper->AddAlias(sen::onto::books::attr::kTitle, "title");
    fMapper->AddAlias(sensei::key::kName, "title");    // add file name as fallback if the title is empty
    fMapper->AddAlias(sen::onto::books::attr::kDate, "publish_year");

    // keep these for later to save another lookup query for relations
    fMapper->AddAlias(OPENLIBRARY_API_AUTHOR_KEY, "author_key");
    fMapper->AddAlias(OPENLIBRARY_API_COVER_KEY, "cover_i");

    status_t result = FetchBookMetadata(&ref, reply);

    if (result == B_CANCELED)
        return result;
    if (result == B_ENTRY_NOT_FOUND) {
        ShowError("OpenLibrary does not know a book like this.");
        return result;
    }
    if (result != B_OK) {
        ShowError("Failed to look up metadata.");
        return result;
    }
    if (fDebugMode) {
        printf("BERT: metadata reply:\n");
        reply->PrintToStream();
    }

    // write back enriched result
    entry_ref resultRef, outRef;
    result = message->FindRef("outRefs", &outRef);
    if (result != B_OK) {
        resultRef = ref;
    } else {
        // create empty output file for result metadata in attributes
        BFile outputFile(&outRef, B_CREATE_FILE | B_READ_WRITE);
        outputFile.Sync();  // ensure file is created so we can access up-to-date attributes below

        // ensure all input attributes are writte to new file
        fOverwrite = true;

        BNode node(&outRef);
        BNodeInfo nodeInfo(&node);

        // always ensure to set correct file type
        result = nodeInfo.SetType(BOOK_MIME_TYPE);
        if (result != B_OK) {
            ShowError("Failed to create book.");
            return result;
        }
        resultRef = outRef;
    }

    // (the file is renamed after its title if it had none: resultRef follows it)
    result = fMapper->MapMsgToAttrs(reply, &resultRef, fOverwrite);
    if (result != B_OK) {
        ShowError("Failed to write back metadata.");
        return result;
    }
    status_t bookResult = B_OK;

    // fetch cover image
    const char* coverId = reply->GetString(OPENLIBRARY_API_COVER_KEY);
    if (coverId != NULL) {
        std::string coverImage;

        result = FetchCover(coverId, &coverImage);
        if (result == B_OK && coverImage.length() > 0) {
            printf("successfully retrieved cover image, writing to thumbnail...\n");

            result = WriteThumbnail(&resultRef, coverImage);
            if (result == B_OK)
                printf("Cover image written to thumbnail successfully.\n");
            else
                printf("error writing thumbnail to file %s: %s\n", resultRef.name, strerror(result));
        } else {
            printf("error fetching cover image, skipping.\n");
        }
    } else {
        printf("could not get cover image ID from result, skipping.\n");
    }

    printf("All Book data retrieved successfully, done.\n");

    // fetch author - todo: demo, outfactor later. OpenLibrary lists all authors, the first one is made (the key is no list: it holds the first).
    BString authorId(reply->GetString(OPENLIBRARY_API_AUTHOR_KEY, ""));

    if (authorId.IsEmpty()) {
        printf("no author in the result, skipping.\n");
        return bookResult;
    }

    BMessage authorResult;
    result = FetchAuthor(authorId.String(), &authorResult);
    if (result != B_OK) {
        printf("could not look up the author %s, skipping: %s\n", authorId.String(), strerror(result));
        return bookResult;
    }

    // create output file for result metadata in attributes
    BString name = authorResult.GetString("META:name", "Unknown Author");
    name.ReplaceAll('/', '-');  // (a file name cannot hold a path)
    printf("creating Author with name '%s'...\n", name.String());

    BFile outputFile(name.String(), B_CREATE_FILE | B_READ_WRITE);
    BEntry entry(name.String());

    entry_ref authorRef;
    result = entry.InitCheck();
    if (result == B_OK)
        result = entry.GetRef(&authorRef);
    if (result != B_OK) {
        printf("could not create author file %s: %s\n", name.String(), strerror(result));
        return bookResult;
    }

    outputFile.Sync();  // ensure file is created so we can access up-to-date attributes below

    BNode node(&authorRef);
    BNodeInfo nodeInfo(&node);
    result = nodeInfo.InitCheck();

    // always ensure to set correct file type
    if (result == B_OK)
        result = nodeInfo.SetType(AUTHOR_MIME_TYPE);
    if (result != B_OK) {
        printf("failed to write back metadata for author: %s\n", strerror(result));
        return bookResult;
    }

    // write info to attrs
    result = fMapper->MapMsgToAttrs(&authorResult, &authorRef, true);  // TODO: fOverwrite
    if (result != B_OK)
        printf("failed to write the metadata of the author: %s\n", strerror(result));

    // fetch author photo
    const char* photoId = authorResult.GetString(OPENLIBRARY_API_COVER_KEY);
    if (photoId != NULL) {
        std::string photo;

        result = FetchPhoto(photoId, &photo);
        if (result == B_OK && photo.length() > 0) {
            result = WriteThumbnail(&authorRef, photo);
            if (result == B_OK)
                printf("Photo written to thumbnail successfully.\n");
            else
                printf("error writing thumbnail to file %s: %s\n", authorRef.name, strerror(result));
        } else {
            printf("error fetching author photo, skipping.\n");
        }
    } else {
        printf("could not get author photo ID from result, skipping.\n");
    }

    return bookResult;
}

/**
 * Chooses one of the books that OpenLibrary found. One is taken, for several the user is asked in a window (sen::ChooseCandidate), the best first;
 * with --auto the best is taken without asking.
 * @param books the answers: messages with the keys "0", "1"...
 * @param wanted the title that was asked for
 * @param chosen the key of the book
 * @return B_OK, B_ENTRY_NOT_FOUND if there is no book, B_CANCELED if the user did not choose
 */
status_t App::ChooseBook(const BMessage& books, const BString& wanted, int32* chosen)
{
    // the answers that can be read, with how good they are
    std::vector<int32> keys;
    std::vector<sen::Candidate> candidates;
    const int32 count = books.CountNames(B_MESSAGE_TYPE);

    for (int32 i = 0; i < count; i++) {
        BString key;
        key << i;
        BMessage doc;
        if (books.FindMessage(key.String(), &doc) != B_OK)
            continue;
        keys.push_back(i);
        candidates.push_back(DescribeBook(doc, ScoreBook(doc, i, count, wanted)));
    }

    if (candidates.empty())
        return B_ENTRY_NOT_FOUND;

    if (candidates.size() == 1) {
        *chosen = keys[0];
        return B_OK;
    }

    // the best one (the first of the best, which is the one that OpenLibrary ranks higher)
    size_t best = 0;
    for (size_t i = 1; i < candidates.size(); i++) {
        if (candidates[i].score > candidates[best].score)
            best = i;
    }

    if (fAuto) {
        printf("got %zu books, taking the best: '%s'.\n", candidates.size(), candidates[best].title.String());
        *chosen = keys[best];
        return B_OK;
    }

    printf("got %zu books, asking the user...\n", candidates.size());
    const int32 picked = sen::ChooseCandidate("Choose a book", "OpenLibrary found several books. Which one is yours?", candidates,
        sen::LoadPluginLogo(kApplicationSignature));
    if (picked < 0)
        return B_CANCELED;

    *chosen = keys[picked];
    return B_OK;
}

status_t App::FetchBookMetadata(const entry_ref* ref, BMessage *resultMsg)
{
    status_t result;

    // gather attributes from ref to map and use as search params
    BMessage inputAttrsMsg;
    result = fMapper->MapAttrsToMsg(ref, &inputAttrsMsg);
    if (result != B_OK) {
        printf("error reading the attributes of the file: %s\n", strerror(result));
        return result;
    }
    if (fDebugMode) {
        printf("input msg from attrs:\n");
        inputAttrsMsg.PrintToStream();
    }

    BMessage paramsMsg;
    result = fBaseEnricher->MapAttrsToServiceParams(&inputAttrsMsg, &paramsMsg);
    if (result != B_OK) {
        printf("error mapping attributes to lookup parameters, aborting.\n");
        return result;
    }

    // what is asked for: the title, to compare the answers with it
    BString wanted(paramsMsg.GetString("title", ""));

    // if the title was taken from the file name, we need to send it as a defined API query parameter "q"
    // because it may contain anything from author name to book title to year
    if (paramsMsg.HasString("title")) {
        BString title;
        if ((title = paramsMsg.GetString("title")) == inputAttrsMsg.GetString(sensei::key::kName)) {
            printf("sending file name '%s' as query param 'q'.\n", title.String());
            paramsMsg.RemoveData("title");
            paramsMsg.AddString("q", title);
        }
    }

    // add advanced fields to result, esp. ISBN, number of pages and lcc classification
    paramsMsg.AddString("fields", "*");
    paramsMsg.AddString("limit", kMaxResults);

    if (fDebugMode) {
        printf("service params msg:\n");
        paramsMsg.PrintToStream();
    }

    BUrl queryUrl(API_BASE_URL "search.json", true);
    BMessage queryResult;

    result = fBaseEnricher->FetchByHttpQuery(queryUrl, &paramsMsg, &queryResult);
    if (result != B_OK) {
        printf("error in remote service call: %s\n", strerror(result));
        return result;
    }

    // get docs
    BMessage books;
    result = queryResult.FindMessage("docs", &books);
    if (result != B_OK) {
        printf("unexpected result format, could not find books in 'docs' list: %s\n", strerror(result));
        if (fDebugMode)
            queryResult.PrintToStream();   // print result msg as is for debugging purposes
        return result;
    }
    if (fDebugMode) {
        printf("received results:\n");
        books.PrintToStream();
    }

    // one book, or the one that the user chooses
    int32 chosen = 0;
    result = ChooseBook(books, wanted, &chosen);
    if (result != B_OK)
        return result;

    // map back result fields to attributes from input ref and write back to return *message
    BMessage bookFound;
    BString chosenKey;
    chosenKey << chosen;
    result = books.FindMessage(chosenKey.String(), &bookFound);
    if (result != B_OK) {
        printf("could not read the chosen book %d: %s\n", (int) chosen, strerror(result));
        return result;
    }
    if (fDebugMode) {
        printf("book result:\n");
        bookFound.PrintToStream();
    }

    BMessage resultBook;

    // convert map values to arrays, they are always indexed by number!
    // TODO: automate this when keys are numbers and values are strings
    BStringList valueMapKeys;
    valueMapKeys.Add("author_name");
    valueMapKeys.Add("author_key");
    valueMapKeys.Add("publisher");
    valueMapKeys.Add("publish_year");
    valueMapKeys.Add("language");
    valueMapKeys.Add("format");
    valueMapKeys.Add("isbn");
    valueMapKeys.Add("lcc");
    valueMapKeys.Add("subject");

    BaseEnricher::ConvertMessageMapsToArray(&bookFound, &resultBook, &valueMapKeys);

	if (!fOverwrite) {
	   // use input attributes as base for result so they get updated and type converted below
	   // todo: we need to merge same values here!
	   resultMsg->Append(inputAttrsMsg);
	}

    result = fBaseEnricher->MapServiceParamsToAttrs(&resultBook, resultMsg);
    if (result != B_OK) {
        printf("error mapping back result: %s\n", strerror(result));
        return result;
    }
    if (fDebugMode) {
        printf("Got attribute result message:\n");
        resultMsg->PrintToStream();
    }

    // update empty or default file name if we may overwrite
    // todo: find a better (i.e. translation safe!) way to determine the default file name
    if (fOverwrite) {
    	// update empty file name with title if exists
    	BString fileName = inputAttrsMsg.GetString(sensei::key::kName, "");
    	if (fileName.Trim().IsEmpty() || fileName == "New Book") {
    		BString title = resultMsg->GetString(sen::onto::books::attr::kTitle, "");
    		if (!title.IsEmpty()) {
    			resultMsg->AddString(sensei::key::kName, title);
    		}
    	}
    }
    return B_OK;
}

// todo: make this on demand and bind to filetype application/x-person
status_t App::FetchAuthor(const char* authorId, BMessage *resultMsg)
{
    if (authorId == NULL || authorId[0] == '\0')
        return B_BAD_VALUE;

    // add author attribute mapping
    fMapper->AddAlias("META:name", "name");
    fMapper->AddAlias("META:birthdate", "birth_date");
    fMapper->AddAlias(OPENLIBRARY_API_COVER_KEY, "photos");

    BUrl queryUrl;
    BMessage queryParams;
    queryParams.AddString("id", authorId);

    status_t result = fBaseEnricher->CreateHttpApiUrl(API_AUTHORS_URL, &queryParams, &queryUrl);
    if (result != B_OK) {
        printf("error in constructing service call: %s\n", strerror(result));
        return result;
    }

    BMessage authorResult;
    result = fBaseEnricher->FetchRemoteJson(queryUrl, authorResult);

    if (result != B_OK) {
        printf("error accessing remote API: %s\n", strerror(result));
        return result;
    }

    printf("got author result:\n");
    authorResult.PrintToStream();

    BMessage author;
    BStringList valueMapKeys;
    valueMapKeys.Add("photos");

    BaseEnricher::ConvertMessageMapsToArray(&authorResult, &author, &valueMapKeys);

    result = fBaseEnricher->MapServiceParamsToAttrs(&author, resultMsg);
    if (result != B_OK) {
        printf("error mapping back result: %s\n", strerror(result));
        return result;
    }
    if (fDebugMode) {
        printf("Got attribute result message:\n");
        resultMsg->PrintToStream();
    }

    // update empty or default file name if we may overwrite
    // todo: find a better (i.e. translation safe!) way to determine the default file name
    if (fOverwrite) {
        // the author is named after the person
        BString personName = resultMsg->GetString("META:name", "");
        if (!personName.IsEmpty()) {
            resultMsg->AddString(sensei::key::kName, personName);
        }
    }

    return B_OK;
}

status_t App::FetchCover(const char* coverId, std::string* coverImage)
{
    BUrl queryUrl;
    BMessage queryParams;
    queryParams.AddString("coverId", coverId);
    queryParams.AddString("size", "M");

    status_t result = fBaseEnricher->CreateHttpApiUrl(API_COVER_URL, &queryParams, &queryUrl);
    if (result != B_OK) {
        printf("error in constructing service call: %s\n", strerror(result));
        return result;
    }

    result = fBaseEnricher->FetchRemoteContent(queryUrl, coverImage);
    if (result != B_OK) {
        printf("error executing remote service call: %s\n", strerror(result));
        return result;
    }

    return B_OK;
}

status_t App::FetchPhoto(const char* photoId, std::string* image)
{
    BUrl queryUrl;
    BMessage queryParams;
    queryParams.AddString("photoId", photoId);
    queryParams.AddString("size", "M");

    status_t result = fBaseEnricher->CreateHttpApiUrl(API_AUTHOR_IMG_URL, &queryParams, &queryUrl);
    if (result != B_OK) {
        printf("error in constructing service call: %s\n", strerror(result));
        return result;
    }

    result = fBaseEnricher->FetchRemoteContent(queryUrl, image);
    if (result != B_OK) {
        printf("error executing remote service call: %s\n", strerror(result));
        return result;
    }

    return B_OK;
}

void App::PrintUsage(const char* errorMsg)
{
    if (errorMsg) {
        std::cerr << "error: " << errorMsg << std::endl;
        fExitCode = 1;
    }
    std::cout << "Usage: bert [-d] [-w] [-a] [-o <output file>] <input file>" << std::endl;
    std::cout << "retrieves book metadata from online sources, currently OpenLibrary.org." << std::endl;
    std::cout << "  -a, --auto    if there are several books, take the best without asking" << std::endl;
    std::cout << "  -d, --debug   print the messages" << std::endl;
    std::cout << "  -w, --wipe    replace attributes that the file has" << std::endl;
    std::cout << "  -o, --output  write the result to this file" << std::endl;
    Quit();
}


void
App::AboutRequested()
{
	sen::ShowAbout();
}
