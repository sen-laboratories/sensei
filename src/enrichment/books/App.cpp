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
}

App::~App()
{
    if (fBaseEnricher) {
        delete fBaseEnricher;
    }
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

    fBaseEnricher = new BaseEnricher(&ref);

    // set up global mapping table (all Strings because it's only about names, not values!)
    fBaseEnricher->AddMapping("Book:ISBN", "isbn");
    fBaseEnricher->AddMapping("Book:Authors", "author_name");
    fBaseEnricher->AddMapping("Book:Languages", "language");
    fBaseEnricher->AddMapping("Book:Publisher", "publisher");
    fBaseEnricher->AddMapping("Media:Title", "title");
    fBaseEnricher->AddMapping("Media:Year", "first_publish_year");
    // keep these for later to save another lookup query for relations
    fBaseEnricher->AddMapping("OL:author_keys", "author_key");
    fBaseEnricher->AddMapping("OL:cover_key", "cover_i");

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
        outputFile.Sync();  // ensure file is created so we can access up-to-date attributes below

        // ensure all input attributes are writte to new file
        fOverwrite = true;

        BNode node(&outRef);
        BNodeInfo nodeInfo(&node);

        // always ensure to set correct file type
        result = nodeInfo.SetType(BOOK_MIME_TYPE);
        if (result != B_OK) {
            BAlert* alert = new BAlert("Error in SEN Book Enricher",
                "Failed to write back metadata.",
                "Oh no.");
            alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
            alert->Go();
            exit(1);
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

    // fetch cover image
    const char* coverId = reply.GetString("OL:cover_key");
    if (coverId != NULL) {
        BBitmap* coverImage;
        size_t   coverImageSize;

        result = FetchCover(coverId, coverImage, &coverImageSize);
        if (result == B_OK) {
            printf("successfully retrieved cover image, writing to thumbnail...\n");

            // write image to thumbnail attribute
            BNode outputNode(&resultRef);
            if ((result = outputNode.InitCheck()) == B_OK) {
                ssize_t size = outputNode.WriteAttr(THUMBNAIL_ATTR_NAME, B_RAW_TYPE, 0, coverImage, coverImageSize);
                if (size < coverImageSize) {
                    printf("error writing thumbnail to file %s: %s\n", resultRef.name, strerror(-size));
                } else {
                    // set thumbnail creation time so it doesn't get removed, use modification time from node
                    time_t modtime;
                    result = outputNode.GetModificationTime(&modtime);
                    if (result == B_OK) {
                        printf("writing thumbnail modification time...\n");
                        size = outputNode.WriteAttr(THUMBNAIL_CREATION_TIME, B_TIME_TYPE, 0, &modtime, sizeof(time_t));
                        if (size < 0 ) {
                            result = -size;
                        }
                    }
                    if (result != B_OK) {
                        printf("error writing thumbnail to %s: %s\n", resultRef.name, strerror(result));
                    }
                }
                if (result == B_OK) {
                    printf("Cover image written to thumbnail successfully.\n");
                    outputNode.Sync();
                }
            } else {
                printf("error opening output file %s: %s\n", resultRef.name, strerror(result));
            }
        } else {
            printf("error fetching cover image, skipping.\n");
        }
    } else {
        printf("could not get cover image ID from result, skipping.\n");
    }

    if (result == B_OK) {
        printf("All Book data retrieved successfully, done.\n");
    }

    reply.AddInt32("resultCode", result);

    printf("reply message:\n");
    reply.PrintToStream();

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

    BUrl queryUrl(API_BASE_URL "search.json");
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
        // todo: implement columnlistview with attributes/params as columns and results in rows
        //       let the user select one *or more* results, so we can gather an entire result set.
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

    // always use input attributes as base for result so they get updated and type converted below!
    resultMsg->Append(inputAttrsMsg);

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

// todo: make this on demand and bind to filetype application/x-person
status_t App::FetchAuthor(BMessage *msgQuery, BMessage *msgResult)
{
    std::string resultBody;
    status_t result = fBaseEnricher->FetchRemoteContent(BUrl(API_AUTHORS_URL), &resultBody);

    if (result != B_OK) {
        printf("error accessing remote API: %s", strerror(result));
        return result;
    }
    return B_OK;
}

status_t App::FetchCover(const char* coverId, BBitmap* coverImage, size_t* imageSize)
{
    BUrl queryUrl;
    BMessage queryParams;
    queryParams.AddString("key", "ID");
    queryParams.AddString("value", coverId);
    queryParams.AddString("size", "M");

    status_t result = fBaseEnricher->CreateHttpApiUrl(API_COVER_URL, &queryParams, &queryUrl);
    if (result != B_OK) {
        printf("error in constructing service call: %s\n", strerror(result));
        return result;
    }

    result = fBaseEnricher->FetchRemoteImage(queryUrl, coverImage, imageSize);
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
    }
    std::cout << "Usage: bert <input file>" << std::endl;
    std::cout << "retrieves book metadata from online sources, currently OpenLibrary.org." << std::endl;
    Quit();
}
