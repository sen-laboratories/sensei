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
#include <Path.h>
#include <private/netservices2/ExclusiveBorrow.h>
#include <private/netservices2/HttpFields.h>
#include <private/netservices2/HttpRequest.h>
#include <private/netservices2/HttpResult.h>
#include <private/netservices2/NetServicesDefs.h>
#include <private/shared/Json.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "App.h"
#include "Sen.h"
#include "Sensei.h"

const char* kApplicationSignature = "application/x-vnd.sen-labs.bert";

using namespace BPrivate::Network;
using namespace std::literals;

App::App() : BApplication(kApplicationSignature)
{
    fHttpSession = new BHttpSession();
}

App::~App()
{
    delete fHttpSession;
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

    bool debug = message->GetBool("debug", false);

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
    if (debug) {
        reply.PrintToStream();
    }
    // we don't expect a reply but run into a race condition with the app
    // being deleted too early, resulting in a malloc assertion failure.
    message->SendReply(&reply, this);
    Quit();
}

status_t App::FetchBookMetadata(const entry_ref* ref, BMessage *message)
{
    // TODO: gather attributes from ref to map and use those as search params

    BString resultJson;

    // TEST author lookup by Thing key
    status_t result = FetchRemoteContent(BUrl("http://openlibrary.org/authors/OL1A.json"), &resultJson);
    if (result != B_OK) {
        printf("error accessing remote API: %s", strerror(result));
        return result;
    }
    printf("GOT JSON:\n");

    BMessage resultMsg;
    BPrivate::BJson::Parse(resultJson, resultMsg);
    resultMsg.PrintToStream();

    // TEST book lookup by ISBN
    resultJson = "";
    result = FetchRemoteContent(BUrl("http://openlibrary.org/search.json?isbn=9783866476158"), &resultJson);
    if (result != B_OK) {
        printf("error accessing remote API: %s", strerror(result));
        return result;
    }
    printf("GOT JSON:\n");

    resultMsg.MakeEmpty();
    BPrivate::BJson::Parse(resultJson, resultMsg);
    resultMsg.PrintToStream();

    // TODO: map result fields to attributes from input ref and write back to return *message

    return B_OK;
}

status_t
App::FetchRemoteContent(const BUrl& httpUrl, BString* resultBody)
{
    std::cout << "fetching REMOTE resource from URL " << httpUrl.UrlString() << "..." << std::endl;
    auto request = BHttpRequest(httpUrl);
    request.SetTimeout(3000 /*ms*/);

    auto fields = request.Fields();
    fields.AddField("User-Agent"sv, "sen-labs.org"sv);
    fields.AddField("Accept"sv, "*/*"sv);

	auto body = make_exclusive_borrow<BMallocIO>();
    BHttpStatus  status;

    try {
        auto result = fHttpSession->Execute(std::move(request), BBorrow<BDataIO>(body));
        // the Status() call will block until full response has been received
        std::cout << "  waiting for result..." << std::endl;
        status = result.Status();
        result.Body();  // synchronize with BBorrow buffer (see HttpSession::Execute docs)
    } catch (const BPrivate::Network::BNetworkRequestError& err) {
        std::cout << "  network ERROR " << err.ErrorCode()
          << " reading from URL " << httpUrl << " - "
          << err.Message() << ", detail: "
          << err.DebugMessage() << std::endl;

        return err.ErrorCode();
    }
    if (status.code >= 200 && status.code <= 400) {
        std::cout << "  HTTP result OK: " << status.code << std::endl;
        try {
            BString bodyString = BString(reinterpret_cast<const char*>(body->Buffer()), body->BufferLength());
            bool hasBody = ! bodyString.IsEmpty();

            std::cout << "  SUCCESS, got " << (! hasBody ? "EMPTY " : "") << "response ";
            if (hasBody) {
                std::cout << "with BODY (" << bodyString.CountChars() << " chars)";
            }
            std::cout << " and status " << status.code << ": " << status.text << std::endl;

            *resultBody = bodyString;
         } catch (const BPrivate::Network::BBorrowError& err) {
            std::cout << "  BorrowError: " << err.Message() << ", origin: " << err.Origin() << std::endl;
            return B_ERROR;
         }
    } else {
        std::cout << "  HTTP error " << status.code
                  << " reading from URL " << httpUrl << " - "
                  << status.text << std::endl;
        return B_ERROR;
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
