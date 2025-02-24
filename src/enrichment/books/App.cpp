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
    status_t result = FetchBookMetadata(const_cast<const entry_ref*>(&ref), &reply);

    if (result != B_OK) {
        BAlert* alert = new BAlert("Error launching SEN Book Enricher",
            "Failed to look up metadata.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();
        return;
    }
    if (fDebugMode) {
        reply.PrintToStream();
    }
    // we don't expect a reply but run into a race condition with the app
    // being deleted too early, resulting in a malloc assertion failure.
    message->SendReply(&reply, this);

    Quit();
}

status_t App::FetchBookMetadata(const entry_ref* ref, BMessage *resultMsg)
{
    // gather attributes from ref to map and use as search params
    BMessage inputAttrsMsg;
    status_t result = BaseEnricher::MapAttrsToMsg(ref, &inputAttrsMsg);
    if (fDebugMode) {
        inputAttrsMsg.PrintToStream();
    }

    BMessage paramsMsg;
    result = MapAttrsToServiceParams(&inputAttrsMsg, &paramsMsg);
    if (result != B_OK) {
        printf("error mapping attributes to lookup parameters, aborting.\n");
        return result;
    }

    if (fDebugMode) {
        printf("got service params:\n");
        paramsMsg.PrintToStream();
    }

    BUrl queryUrl("http://openlibrary.org/search.json");
    result = fBaseEnricher->FetchByQuery(queryUrl, &paramsMsg, resultMsg);
    if (result != B_OK) {
        printf("error in remote service call: %s\n", strerror(result));
        return result;
    }

    // get docs
    BMessage books;
    double numFound = -1;

    result = resultMsg->FindDouble("numFound", &numFound);
    if (result == B_OK) {
        result = resultMsg->FindMessage("docs", &books);
    }
    if (fDebugMode) {
        if (result == B_OK) {
            printf("received %f results:\n", numFound);
            books.PrintToStream();
        } else {
            printf("unexpected result format, could not find books in 'docs' list: %s\n", strerror(result));
            // print result msg as is for debugging purposes
            resultMsg->PrintToStream();
            return result;
        }
    }

    if (numFound > 1) {
        // user needs to select a result
        printf("please select a book...");
    }

    // map back result fields to attributes from input ref and write back to return *message

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

//
// helper methods
//

/**
* map well known entity attributes to service parameters
*/
status_t
App::MapAttrsToServiceParams(BMessage *attrMsg, BMessage *serviceParamMsg)
{
    serviceParamMsg->AddString("isbn", attrMsg->GetString("Book:ISBN"));
    // fall back to file name if no title given
    serviceParamMsg->AddString("title", attrMsg->GetString("Media:Title", attrMsg->GetString("name")));
    // map author(s) / contributors
    BString authors = attrMsg->GetString("Book:Authors");
    if (!authors.IsEmpty()) {
        BString author;
        BStringList authorNames;
        authors.Split(",", true, authorNames);
        if (authorNames.CountStrings() > 1) {
            author = authorNames.First();
            serviceParamMsg->AddString("contributor", authorNames.StringAt(1));
            // LATER: support multiple contributors, if this is supported by OpenLibrary?
        } else {
            author = authors;
        }
        serviceParamMsg->AddString("author_name", author);
    }
    serviceParamMsg->AddString("publish_year", attrMsg->GetString("Media:Year"));
    serviceParamMsg->AddString("publisher", attrMsg->GetString("Book:Publisher"));
    // take first language if any
    BString languages = attrMsg->GetString("Book:Languages");
    if (!languages.IsEmpty()) {
        BString language;
        BStringList langs;
        languages.Split(",", true, langs);
        if (langs.CountStrings() > 1) {
            language = langs.StringAt(0);
        } else {
            language = languages;
        }
        serviceParamMsg->AddString("language", language);
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
