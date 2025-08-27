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
    fMapper = new MappingUtil();
}

App::~App()
{
    if (fBaseEnricher) {
        delete fBaseEnricher;
    }
    delete fMapper;
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
    fOverwrite = message->GetBool("wipe", true);

    fBaseEnricher = new BaseEnricher(&ref, fMapper);

    // set up global mapping table (all Strings because it's only about names, not values!)
    fMapper->AddAlias("Book:ISBN", "isbn");
    fMapper->AddAlias("Book:Authors", "author_name");
    fMapper->AddAlias("Book:Languages", "language");
    fMapper->AddAlias("Book:Publisher", "publisher");
    fMapper->AddAlias("Book:Format", "format");
    fMapper->AddAlias("Book:Subjects", "subject");
    fMapper->AddAlias("Book:Class", "lcc");
    fMapper->AddAlias("Book:Pages", "number_of_pages_median");
    fMapper->AddAlias("Media:Title", "title");
    fMapper->AddAlias(SENSEI_NAME, "title");    // add file name as fallback if Media:Title is empty
    fMapper->AddAlias("Book:Year", "publish_year");

    // keep these for later to save another lookup query for relations
    fMapper->AddAlias(OPENLIBRARY_API_AUTHOR_KEY, "author_key");
    fMapper->AddAlias(OPENLIBRARY_API_COVER_KEY, "cover_i");

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
        printf("BERT: metadata reply:\n");
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
                "Failed to create book.",
                "Oh no.");
            alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
            alert->Go();
            exit(1);
        }
        resultRef = outRef;
    }

    result = fMapper->MapMsgToAttrs(&reply, &resultRef, fOverwrite);
    if (result != B_OK) {
        BAlert* alert = new BAlert("Error in SEN Book Enricher",
            "Failed to write back metadata.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();
        exit(1);
    }

    // fetch cover image
    const char* coverId = reply.GetString(OPENLIBRARY_API_COVER_KEY);
    if (coverId != NULL) {
        std::string coverImage;

        result = FetchCover(coverId, &coverImage);
        if (result == B_OK && coverImage.length() > 0) {
            printf("successfully retrieved cover image, writing to thumbnail...\n");

            // write image to thumbnail attribute
            BNode outputNode(&resultRef);
            if ((result = outputNode.InitCheck()) == B_OK) {
                ssize_t size = outputNode.WriteAttr(THUMBNAIL_ATTR_NAME, B_RAW_TYPE, 0, coverImage.c_str(), coverImage.length());

                if (size < coverImage.length()) {
                    printf("error writing thumbnail to file %s: %s\n", resultRef.name, strerror(-size));
                } else {
                    // set thumbnail creation time so it doesn't get removed, use modification time from node
                    time_t modtime;
                    result = outputNode.GetModificationTime(&modtime);
                    modtime++;  // thumbnail creation time needs to be after file change time to be kept.

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

    // fetch author - todo: demo, outfactor later
    const char* authorId = reply.GetString(OPENLIBRARY_API_AUTHOR_KEY);
    BMessage authorResult;

    result = FetchAuthor(authorId, &authorResult);

    if (result == B_OK) {
        // create output file for result metadata in attributes
        BString name = authorResult.GetString("META:name", "Unknown Author");
        printf("creating Author with name '%s'...\n", name.String());

        BFile outputFile(name.String(), B_CREATE_FILE | B_READ_WRITE);
        BEntry entry(name);

        entry_ref authorRef;
        entry.GetRef(&authorRef);

        result = entry.InitCheck();
        if (result != B_OK) {
            printf("could not create author file %s: %s\n", name.String(), strerror(result));
            exit(1);
        }

        BaseEnricher authorEnricher(&authorRef, fMapper);
        outputFile.Sync();  // ensure file is created so we can access up-to-date attributes below

        // ensure all input attributes are writte to new file
        fOverwrite = true;
        BNode node(&authorRef);
        BNodeInfo nodeInfo(&node);
        result = nodeInfo.InitCheck();

        // always ensure to set correct file type
        if (result == B_OK) result = nodeInfo.SetType(AUTHOR_MIME_TYPE);
        if (result != B_OK) {
            BAlert* alert = new BAlert("Error in SEN Book Enricher",
                "Failed to write back metadata for author.",
                "Oh no.");
            alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
            alert->Go();
            exit(1);
        }
        // write info to attrs
        result = fMapper->MapMsgToAttrs(&authorResult, &authorRef, true);  // TODO: fOverwrite

        // fetch author photo
        const char* photoId = authorResult.GetString(OPENLIBRARY_API_COVER_KEY);
        if (photoId != NULL) {
            std::string photo;

            result = FetchPhoto(photoId, &photo);
            if (result == B_OK && photo.length() > 0) {
                printf("successfully retrieved cover image, writing to thumbnail...\n");

                // write image to thumbnail attribute
                BNode outputNode(&authorRef);
                if ((result = outputNode.InitCheck()) == B_OK) {
                    ssize_t size = outputNode.WriteAttr(THUMBNAIL_ATTR_NAME, B_RAW_TYPE, 0, photo.c_str(), photo.length());

                    if (size < photo.length()) {
                        printf("error writing thumbnail to file %s: %s\n", authorRef.name, strerror(-size));
                    } else {
                        // set thumbnail creation time so it doesn't get removed, use modification time from node
                        time_t modtime;
                        result = outputNode.GetModificationTime(&modtime);
                        modtime++;  // thumbnail creation time needs to be after file change time to be kept.

                        if (result == B_OK) {
                            printf("writing thumbnail modification time...\n");
                            size = outputNode.WriteAttr(THUMBNAIL_CREATION_TIME, B_TIME_TYPE, 0, &modtime, sizeof(time_t));
                            if (size < 0 ) {
                                result = -size;
                            }
                        }
                        if (result != B_OK) {
                            printf("error writing thumbnail to %s: %s\n", authorRef.name, strerror(result));
                        }
                    }
                    if (result == B_OK) {
                        printf("Cover image written to thumbnail successfully.\n");
                        outputNode.Sync();
                    }
                } else {
                    printf("error opening output file %s: %s\n", authorRef.name, strerror(result));
                }
            } else {
                printf("error fetching cover image, skipping.\n");
            }
        } else {
            printf("could not get cover image ID from result, skipping.\n");
        }
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
    result = fMapper->MapAttrsToMsg(ref, &inputAttrsMsg);
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

    // if the title was taken from the file name, we need to send it as a defined API query parameter "q"
    // because it may contain anything from author name to book title to year
    if (paramsMsg.HasString("title")) {
        BString title;
        if ((title = paramsMsg.GetString("title")) == inputAttrsMsg.GetString(SENSEI_NAME)) {
            printf("sending file name '%s' as query param 'q'.\n", title.String());
            paramsMsg.RemoveData("title");
            paramsMsg.AddString("q", title);
        }
    }

    // add advanced fields to result, esp. ISBN, number of pages and lcc classification
    paramsMsg.AddString("fields", "*");

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
        printf("got %f books, please select... TBI\n", numFound);
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
    	BString fileName = inputAttrsMsg.GetString(SENSEI_NAME, "");
    	if (fileName.Trim().IsEmpty() || fileName == "New Book") {
    		BString title = resultMsg->GetString("Media:Title", "");
    		if (!title.IsEmpty()) {
    			resultMsg->AddString(SENSEI_NAME, title);
    		}
    	}
    }
    return B_OK;
}

// todo: make this on demand and bind to filetype application/x-person
status_t App::FetchAuthor(const char* authorId, BMessage *resultMsg)
{
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
        printf("error accessing remote API: %s", strerror(result));
        return result;
    }

    printf("got author result:\n");
    authorResult.PrintToStream();

    BMessage author;
    BStringList valueMapKeys;
    valueMapKeys.Add("photos");

    BaseEnricher::ConvertMessageMapsToArray(&authorResult, &author, &valueMapKeys);

	if (!fOverwrite) {
	   // use input attributes as base for result so they get updated and type converted below
	   // todo: we need to merge same values here!
	  // resultMsg->Append(inputAttrsMsg);
	}

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
    	// update empty file name with title if exists
    	// todo: check BString fileName = inputAttrsMsg.GetString(SENSEI_NAME_ATTR, "");
    	//       if (fileName.Trim().IsEmpty() || fileName == "New Book") {
    		BString personName = resultMsg->GetString("META:name", "");
    		if (!personName.IsEmpty()) {
    			resultMsg->AddString(SENSEI_NAME, personName);
    		}
    	//}
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
    }
    std::cout << "Usage: bert <input file>" << std::endl;
    std::cout << "retrieves book metadata from online sources, currently OpenLibrary.org." << std::endl;
    Quit();
}
