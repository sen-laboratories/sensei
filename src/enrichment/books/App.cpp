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
    if (strncmp(argv[1], "-h", 2) == 0 || strncmp(argv[1], "--help", 6) == 0) {
        PrintUsage();
        return;
    }

    // real argument parsing
    int argIndex = 1;
    bool debug = false;

    if (strncmp(argv[1], "-d", 2) == 0 || strncmp(argv[1], "--debug", 7) == 0) {
        argIndex++;
        debug = true;
    }

    if (argIndex > argc+1) {
        PrintUsage("Missing input file." );
        return;
    }

    BMessage refsMsg(B_REFS_RECEIVED);
    BEntry entry(argv[argIndex]);

    entry_ref ref;
    entry.GetRef(&ref);
    refsMsg.AddRef("refs", &ref);

    if (debug) {
        refsMsg.AddBool("debug", true);
    }

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

    BMessage reply(SENSEI_MESSAGE_RESULT);
    status_t result = FetchBookMetadata(&ref, &reply);

    if (result != B_OK) {
        BAlert* alert = new BAlert("Error launching SEN Book Enricher",
            "Failed to look up metadata.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();
        Quit();
        return;
    }
    if (fDebugMode) {
        printf("reply:\n");
        reply.PrintToStream();
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

    BMessage resultAttrMsg;
    BMessage resultBook;

    // convert map values to arrays, they are always indexed by number!
    BStringList valueMapKeys;
    valueMapKeys.Add("author_name");
    valueMapKeys.Add("author_key");
    valueMapKeys.Add("language");
    BaseEnricher::ConvertMessageMapsToArray(&bookFound, &resultBook, &valueMapKeys);

    result = fBaseEnricher->MapServiceParamsToAttrs(&resultBook, &resultAttrMsg);
    if (result != B_OK) {
        printf("error mapping back result: %s\n", strerror(result));
        return result;
    }
    if (fDebugMode) {
        printf("Got attribute result message:\n");
        resultAttrMsg.PrintToStream();
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
