/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */
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
#include "Authors.h"
#include "BookRanking.h"
#include "../../common/Notice.h"
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

// (a notification, not a dialog: the plugin is not held up and nothing stays on the screen)
void App::ShowError(const char* message)
{
    sensei::Notify("SEN Book Enricher", "Book not enriched", message, false);
}

void App::RefsReceived(BMessage *message)
{
    BMessage reply(sensei::cmd::kResult);
    status_t result = Enrich(message, &reply);

    if (result == B_CANCELED)
        printf("cancelled, nothing was changed.\n");
    else if (result != B_OK)
        fExitCode = 1;
    else if (!fAuto)
        sensei::Notify("SEN Book Enricher", "Book enriched", fEnrichedRef.name, true, &fEnrichedRef);   // (a script that asked for --auto does not need to be told)

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
        BString text("OpenLibrary has no book called \"");
        text << fWanted << "\". Set the title of the file (dc:title), or name the file after the book.";
        ShowError(text.String());
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
    fEnrichedRef = resultRef;
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

    // the authors: each one a person next to the book, with its photo as the thumbnail, and related to the book (see Authors.h)
    if (fAuthorKeys.IsEmpty()) {
        printf("no author in the result, skipping.\n");
    } else {
        AuthorMaker authors(fBaseEnricher, fOverwrite, fDebugMode);
        int32 made = authors.MakeAuthors(resultRef, fAuthorKeys, fAuthorNames);
        printf("%d of %d authors made.\n", (int) made, (int) fAuthorKeys.CountStrings());
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

    // which of them have the title that was asked for (an answer of another book is no help, however well OpenLibrary ranks it)
    std::vector<bool> matches;
    size_t matching = 0;
    for (int32 key : keys) {
        BString index;
        index << key;
        BMessage doc;
        const bool matched = books.FindMessage(index.String(), &doc) == B_OK && TitleMatches(doc, wanted);
        matches.push_back(matched);
        if (matched)
            matching++;
    }

    // the best one (the first of the best, which is the one that OpenLibrary ranks higher)
    size_t best = candidates.size();
    for (size_t i = 0; i < candidates.size(); i++) {
        if (fAuto && !matches[i])
            continue;
        if (best == candidates.size() || candidates[i].score > candidates[best].score)
            best = i;
    }

    if (fAuto) {
        if (best == candidates.size()) {
            printf("got %zu books, none of them with the title '%s'.\n", candidates.size(), wanted.String());
            return B_ENTRY_NOT_FOUND;
        }
        printf("got %zu books, taking the best: '%s'.\n", candidates.size(), candidates[best].title.String());
        *chosen = keys[best];
        return B_OK;
    }

    // one answer, and it is the book that was asked for: nothing to ask
    if (candidates.size() == 1 && matching == 1) {
        *chosen = keys[0];
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
    fWanted = wanted;

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

    // the authors as OpenLibrary has them (the keys and the names in the same order): the attributes of the book keep only the first key, the
    // authors are made from these, see Authors.h
    fAuthorKeys.MakeEmpty();
    fAuthorNames.MakeEmpty();
    const char* authorValue;
    for (int32 index = 0; resultBook.FindString("author_key", index, &authorValue) == B_OK; index++)
        fAuthorKeys.Add(authorValue);
    for (int32 index = 0; resultBook.FindString("author_name", index, &authorValue) == B_OK; index++)
        fAuthorNames.Add(authorValue);

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
