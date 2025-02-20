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
#include <fs_attr.h>
#include <NodeInfo.h>
#include <MimeType.h>
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
    status_t result = MapAttrsToMsg(ref, &inputAttrsMsg);
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

    result = FetchByQuery(&paramsMsg, resultMsg);
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

status_t App::FetchByQuery(BMessage *msgQuery, BMessage *msgResult)
{
    BUrl queryUrl("http://openlibrary.org/search.json");
    BString request;
    status_t result;
    // add all message data to Url as request params
    for (int32 i = 0; i < msgQuery->CountNames(B_ANY_TYPE); i++) {
        char *key;
        uint32 type;
        result = msgQuery->GetInfo(B_ANY_TYPE, i, &key, &type);

        if (result == B_OK) {
            const void* data;
            ssize_t dataSize;
            result = msgQuery->FindData(reinterpret_cast<const char*>(key), type, i, &data, &dataSize);
            if (result == B_OK && dataSize > 0) {
                if (! request.IsEmpty()) {
                    request << "&";
                }
                BString value;
                switch(type){
                    case B_STRING_TYPE:
                        value << (const char*) data;
                        break;
                    case B_INT32_TYPE:
                        value << (*(const int32*) data);
                        break;
                    case B_BOOL_TYPE:
                        value << *((bool*) data);
                        break;
                    default:
                        printf("unsupported type, skipping.");
                        break;
                }
                request << key << "=" << BUrl::UrlEncode(value);
            } else {
                printf("failed to fetch message data for key '%s': %s\n", key, strerror(result));
            }
        }
    }
    queryUrl.SetRequest(request);

    BString resultBody;
    result = FetchRemoteContent(queryUrl, &resultBody);

    if (result != B_OK) {
        printf("error accessing remote API: %s", strerror(result));
        return result;
    }

    BJson::Parse(resultBody, *msgResult);

    return B_OK;
}

status_t App::FetchAuthor(BMessage *msgQuery, BMessage *msgResult)
{
    BString resultJson;
    status_t result = FetchRemoteContent(BUrl("http://openlibrary.org/authors/OL1A.json"), &resultJson);

    if (result != B_OK) {
        printf("error accessing remote API: %s", strerror(result));
        return result;
    }
    return B_OK;
}

status_t App::FetchCover(BMessage *msgQuery, BMessage *msgResult)
{
    BString resultJson;
    status_t result = FetchRemoteContent(BUrl("http://openlibrary.org/search.json?isbn=9783866476158"), &resultJson);

    if (result != B_OK) {
        printf("error accessing remote API: %s", strerror(result));
        return result;
    }
    return B_OK;
}

//
// helper methods
//

status_t App::MapAttrsToMsg(const entry_ref* ref, BMessage *attrMsg)
{
    status_t result;
    BNode node(ref);

    result = node.InitCheck();
	if (result != B_OK) {
        printf("failed to read input file from ref %s: %s\n", ref->name, strerror(result));
		return result;
    }

	char *attrName = new char[B_ATTR_NAME_LENGTH];
    BMessage mimeInfoMsg;

    result = GetMimeTypeAttrs(ref, &mimeInfoMsg);
    if (result != B_OK) {
        return result;
    }

    if (fDebugMode) {
        printf("got MIME attribute info:\n");
        mimeInfoMsg.PrintToStream();
    }

    // always add file name as fallback for title
    attrMsg->AddString("name", ref->name);

    attr_info attrInfo;
	while (node.GetNextAttrName(attrName) == B_OK) {
        int32 attrType = mimeInfoMsg.GetInt32(attrName, -1);
		if (attrType < 0) {
            // skip internal/custom attributes
            printf("skipping internal/custom attribute %s.\n", attrName);
            continue;
        }

        node.GetAttrInfo(attrName, &attrInfo);
        char attrValue[attrInfo.size + 1];

        ssize_t bytesRead = node.ReadAttr(
            attrName,
            attrType,
            0,
            attrValue,
            attrInfo.size);

        if (bytesRead == 0) {
            printf("attribute %s has unexpeted type %u in file %s.\n", attrName, attrType, ref->name);
            return B_ERROR;
        } else if (bytesRead < 0) {
            printf("failed to read attribute value for attribute %s of file %s.\n", attrName, ref->name);
            return B_ERROR;
        }

        attrMsg->AddData(attrName, attrType, attrValue, bytesRead);
	}

    return B_OK;
}

status_t App::GetMimeTypeAttrs(const entry_ref* ref, BMessage *mimeAttrMsg)
{
    BNode node(ref);
    BMimeType mimeType;
    char type[B_MIME_TYPE_LENGTH];

    BNodeInfo nodeInfo(&node);
    status_t result = nodeInfo.GetType(type);

    if (result == B_OK) {
        mimeType.SetType(type);
    } else {
        result = BMimeType::GuessMimeType(ref, &mimeType);
        if (result != B_OK) {
            printf("failed to get MIME info for input file %s: %s\n", ref->name, strerror(result));
            return result;
        }
    }
    if (fDebugMode) {
        printf("got MIME type '%s' for file '%s'.\n", mimeType.Type(), ref->name);
    }

    BMessage attrInfoMsg;
    result = mimeType.GetAttrInfo(&attrInfoMsg);
    if (result != B_OK) {
        printf("failed to get attrInfo for MIME type %s: %s\n", mimeType.Type(), strerror(result));
        return result;
    }

    if (fDebugMode) {
        printf("attrInfoMsg:\n");
        attrInfoMsg.PrintToStream();
    }

    // fill in name and type and return as msg
    for (int32 info = 0; info < attrInfoMsg.CountNames(B_STRING_TYPE); info++) {
        const char* attrName = attrInfoMsg.GetString("attr:name", info, NULL);
        if (attrName == NULL) {
            printf("MIME type not in MIME DB: could not get 'attr:name'! Aborting.\n");
            return B_ERROR;
        }
        int32 typeCode = attrInfoMsg.GetInt32("attr:type", info, B_STRING_TYPE);

        // add name/type mapping for MIME type
        mimeAttrMsg->AddInt32(attrName, typeCode);
    }
    return B_OK;
}

bool App::IsInternalAttr(BString* attrName)
{
    return attrName->StartsWith("be:") ||
           attrName->StartsWith("BEOS:") ||
           attrName->StartsWith("META:") ||
           attrName->StartsWith("_trk/") ||
           // application specific metadata
           attrName->StartsWith("pe-info") ||
           attrName->StartsWith("PDF:") ||
           attrName->StartsWith("bepdf:");
}

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
