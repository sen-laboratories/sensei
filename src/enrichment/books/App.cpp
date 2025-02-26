/*
 * Copyright 2024, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 *
 * BERT - Book EnRichment Tool
 * a simple SEN plugin for grabbing metadata for books, can also be used as a standalone tool.
 */
#include <Alert.h>
#include <Entry.h>
#include <Errors.h>
#include <StringList.h>
#include <fs_attr.h>
#include <NodeInfo.h>
#include <MimeType.h>
#include <Path.h>

#include <iostream>

#include "App.h"
#include "Sen.h"
#include "Sensei.h"

const char* kApplicationSignature = "application/x-vnd.sen-labs.bert";

App::App() : BApplication(kApplicationSignature)
{
    fBaseEnricher = new BaseEnricher();
    // set up global mapping table (all Strings because it's only about names, not values!)
    fBaseEnricher->AddMapping("Book:ISBN", "isbn");
    fBaseEnricher->AddMapping("Book:Authors", "author_name");
    fBaseEnricher->AddMapping("Book:Languages", "language");
    fBaseEnricher->AddMapping("Book:Publisher", "publisher");
    fBaseEnricher->AddMapping("Media:Title", "title");
    fBaseEnricher->AddMapping("Media:Year", "first_publish_year");
    // keep these for later to save another lookup query for relations
    fBaseEnricher->AddMapping("OL:author_keys", "author_key");
}

App::~App()
{
    delete fBaseEnricher;
}

int main()
{
	App* app = new App();
    if (app->InitCheck() != B_OK) {
        return 1;
    }

	app->Run();

    delete app;
	return 0;
}

void App::ArgvReceived(int32 argc, char ** argv) {
    // sanity checks
    if (argc < 2) {
        PrintUsage();
        return;
    }

    // real argument parsing
    int argIndex = 1;
    bool debug = false;
    bool wipe = false;
    BString inputPath;
    BString outputPath;

    while (argIndex < argc) {   // last argument is always input file
        const char* arg = argv[argIndex];
        printf("handling argument #%d: '%s'...\n", argIndex, arg);

        if (strncmp(arg, "-h", 2) == 0 || strncmp(arg, "--help", 6) == 0) {
            PrintUsage();
            exit(1);
        } else if (strncmp(arg, "-d", 2) == 0 || strncmp(arg, "--debug", 7) == 0) {
            debug = true;
        } else if (strncmp(arg, "-w", 2) == 0 || strncmp(arg, "--wipe", 6) == 0) {
            wipe = true;
        } else if (strncmp(arg, "-o", 2) == 0 || strncmp(arg, "--output", 8) == 0) {
            argIndex++; // advance to next argument after option switch
            outputPath = argv[argIndex];
        } else {
            if (strncmp(arg, "-", 1) == 0 ) {
                BString errorMsg("unknown parameter ");
                errorMsg.Append(arg);
                PrintUsage(errorMsg.String());

                exit(1);
            }
            inputPath = arg;
        }

        argIndex++;
    }

    if (inputPath.IsEmpty()) {
        PrintUsage("Missing input file." );
        exit(1);
    }

    BMessage refsMsg(B_REFS_RECEIVED);
    BEntry inputEntry(inputPath);
    entry_ref ref;

    inputEntry.GetRef(&ref);
    refsMsg.AddRef("refs", &ref);

    if (outputPath != NULL) {
        BEntry outputEntry(outputPath);
        entry_ref outputRef;
        outputEntry.GetRef(&outputRef);

        refsMsg.AddRef("outRefs", &outputRef);
    }

    if (debug) {
        refsMsg.AddBool("debug", true);
    }
    if (wipe) {
        refsMsg.AddBool("wipe", true);
    }

    //TEST
    printf("constructed refs msg:\n");
    refsMsg.PrintToStream();

    RefsReceived(&refsMsg);
}

void App::RefsReceived(BMessage *message)
{
    entry_ref ref;

    if (message->FindRef("refs", &ref) != B_OK) {
        BAlert* alert = new BAlert("Error launching SEN Book Enricher",
            "Failed to resolve input file.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();
        return;
    }

    fDebugMode = message->GetBool("debug", false);
    fOverwrite = message->GetBool("wipe", false);

    BMessage reply(SENSEI_MESSAGE_RESULT);
    status_t result = FetchBookMetadata(&ref, &reply);

    if (result != B_OK) {
        BAlert* alert = new BAlert("Error launching SEN Book Enricher",
            "Failed to look up metadata.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();
        exit(1);
    }
    if (fDebugMode) {
        printf("reply:\n");
        reply.PrintToStream();
    }

    // write back enriched result
    entry_ref resultRef, outRef;
    result = message->FindRef("outRefs", &outRef);
    if (result != B_OK) {
        resultRef = ref;
    } else {
        // create empty output file for result metadata in attributes
        BFile outputFile(&outRef, B_CREATE_FILE | B_READ_WRITE);
        outputFile.Sync();

        BNode node(&outRef);
        BNodeInfo nodeInfo(&node);
        char type[B_MIME_TYPE_LENGTH];

        result = nodeInfo.GetType(type);
        if (fOverwrite || (result != B_OK)) {
            if (fOverwrite || (result == B_ENTRY_NOT_FOUND)) {
                result = nodeInfo.SetType("entity/book");
            }
            if (result != B_OK) {
                BAlert* alert = new BAlert("Error in SEN Book Enricher",
                    "Failed to write back metadata.",
                    "Oh no.");
                alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
                alert->Go();
                exit(1);
            }
        }
        resultRef = outRef;
    }

    result = fBaseEnricher->MapMsgToAttrs(&reply, &resultRef, fOverwrite);
    if (result != B_OK) {
        BAlert* alert = new BAlert("Error in SEN Book Enricher",
            "Failed to write back metadata.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();
        exit(1);
    }

    // we don't expect a reply but run into a race condition with the app
    // being deleted too early, resulting in a malloc assertion failure.
    message->SendReply(&reply, this);

    Quit();
}

status_t App::FetchBookMetadata(const entry_ref* ref, BMessage *resultMsg)
{
    status_t result;

    // gather attributes from ref to map and use as search params
    BMessage inputAttrsMsg;
    result = fBaseEnricher->MapAttrsToMsg(ref, &inputAttrsMsg);
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

    if (fDebugMode) {
        printf("service params msg:\n");
        paramsMsg.PrintToStream();
    }

    BUrl queryUrl("http://openlibrary.org/search.json");
    BMessage queryResult;

    result = fBaseEnricher->FetchByHttpQuery(queryUrl, &paramsMsg, &queryResult);
    if (result != B_OK) {
        printf("error in remote service call: %s\n", strerror(result));
        return result;
    }

    // get docs
    BMessage books;
    double numFound = -1;

    result = queryResult.FindDouble("num_found", &numFound);
    if (result == B_OK) {
        result = queryResult.FindMessage("docs", &books);
    }
    if (fDebugMode) {
        if (result == B_OK) {
            printf("received %f results:\n", numFound);
            books.PrintToStream();
        } else {
            printf("unexpected result format, could not find books in 'docs' list: %s\n", strerror(result));
            // print result msg as is for debugging purposes
            queryResult.PrintToStream();
            return result;
        }
    }

    if (numFound > 1) {
        // user needs to select a result
        printf("please select a book... TBI\n");
    }

    // map back result fields to attributes from input ref and write back to return *message
    BMessage bookFound;
    books.FindMessage("0", &bookFound);  // already error-checked above
    if (fDebugMode) {
        printf("book result:\n");
        bookFound.PrintToStream();
    }

    BMessage resultBook;

    // convert map values to arrays, they are always indexed by number!
    BStringList valueMapKeys;
    valueMapKeys.Add("author_name");
    valueMapKeys.Add("author_key");
    valueMapKeys.Add("language");
    BaseEnricher::ConvertMessageMapsToArray(&bookFound, &resultBook, &valueMapKeys);

    result = fBaseEnricher->MapServiceParamsToAttrs(&resultBook, resultMsg);
    if (result != B_OK) {
        printf("error mapping back result: %s\n", strerror(result));
        return result;
    }
    if (fDebugMode) {
        printf("Got attribute result message:\n");
        resultMsg->PrintToStream();
    }
    return B_OK;
}

status_t App::FetchAuthor(BMessage *msgQuery, BMessage *msgResult)
{
    std::string resultBody;
    status_t result = fBaseEnricher->FetchRemoteContent(
        BUrl("http://openlibrary.org/authors/OL1A.json"), &resultBody);

    if (result != B_OK) {
        printf("error accessing remote API: %s", strerror(result));
        return result;
    }
    return B_OK;
}

status_t App::FetchCover(BMessage *msgQuery, BMessage *msgResult)
{
    std::string resultBody;
    status_t result = fBaseEnricher->FetchRemoteContent(
        BUrl("http://openlibrary.org/search.json?isbn=9783866476158"), &resultBody);

    if (result != B_OK) {
        printf("error accessing remote API: %s", strerror(result));
        return result;
    }
    return B_OK;
}

void App::PrintUsage(const char* errorMsg)
{
    if (errorMsg) {
        std::cerr << "error: " << errorMsg << std::endl;
    }
    std::cout << "Usage: bert <input file>" << std::endl;
    std::cout << "retrieves book metadata from online sources, currently OpenLibrary.org." << std::endl;
    Quit();
}
