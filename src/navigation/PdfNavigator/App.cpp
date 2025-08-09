/*
 * Copyright 2024-2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Alert.h>
#include <AppFileInfo.h>
#include <Errors.h>
#include <iostream>
#include <MimeType.h>
#include <Roster.h>

#include "App.h"
#include "Sen.h"

const char* kApplicationSignature = "application/x-vnd.sen-labs.PdfNavigator";

App::App() : BApplication(kApplicationSignature)
{
}

App::~App()
{
}

// intended for testing
void App::ArgvReceived(int32 argc, char ** argv) {
    if (argc < 1) {
        std::cerr << "Invalid usage, simply provide PDF file as 1st argument." << std::endl;
        return;
    }
    int32 page = 0;
    if (argc > 1) {
        page = atoi(argv[2]);
    }

    BMessage refsMsg(B_REFS_RECEIVED);
    BEntry entry(argv[1]);
    entry_ref ref;

    entry.GetRef(&ref);
    refsMsg.AddRef("refs", &ref);

    if (page > 0) {
        refsMsg.AddInt32("page", page);
    }

    RefsReceived(&refsMsg);
}

void App::RefsReceived(BMessage *message)
{
    entry_ref ref;

    if (message->FindRef("refs", &ref) != B_OK) {
        BAlert* alert = new BAlert("Error launching SEN Relation Navigator",
            "Failed to resolve relation target.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();

        Quit();
        return;
    }

    status_t result;
    BMessage argsMsg;

    result = message->FindMessage(SEN_RELATION_PROPERTIES, &argsMsg);
    if (result == B_OK) {
        result = MapRelationPropertiesToArguments(&argsMsg);
    }
    if (result == B_OK) {
        message->RemoveData(SEN_RELATION_PROPERTIES);
        message->Append(argsMsg);
    }

    if (result != B_OK) {
        if (result != B_NAME_NOT_FOUND) {
            BString error("Failed to map launch arguments!\nReason: ");
            BAlert* alert = new BAlert("SEN Relation Navigator",
                error << strerror(result), "OK");
            alert->SetFlags(alert->Flags() | B_STOP_ALERT | B_CLOSE_ON_ESCAPE);
            alert->Go();
            Quit();
            return;
        } else {    // warn but continue
            BString error("Could not map launch arguments: no known parameter found!");
            BAlert* alert = new BAlert("SEN Relation Navigator",
                error << strerror(result), "OK");
            alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
            alert->Go();
        }
    }

    // we need to build our own refs received message so we can send the properties with it
    entry_ref appRef;
    result = be_roster->FindApp(&ref, &appRef);
    if (result == B_OK) {
        if (! be_roster->IsRunning(&appRef)) {
            result = be_roster->Launch(&appRef, message);
        } else {
            char appSig[B_MIME_TYPE_LENGTH];
            BFile appFile(&appRef, B_READ_ONLY);
            if (appFile.InitCheck() == B_OK) {
                BAppFileInfo appFileInfo(&appFile);
                if (appFileInfo.InitCheck() == B_OK) {
                    if (appFileInfo.GetSignature(appSig) == B_OK) {
                        LOG("got MIME type %s for ref %s\n", appSig, appRef.name);
                        // send message to running instance for a more seamless experience
                        BMessenger appMess(appSig);
                        appMess.SendMessage(message);
                    }
                }
            } else {
                LOG("failed to get MIME Type for ref %s: %s\n", appRef.name, strerror(result));
            }
        }
    }
    if (result != B_OK && result != B_ALREADY_RUNNING) {
            BString error("Could not launch target application: ");
            error << strerror(result);
            BAlert* alert = new BAlert("SEN Relation Navigator",
                error << strerror(result), "OK");
            alert->SetFlags(alert->Flags() | B_STOP_ALERT | B_CLOSE_ON_ESCAPE);
            alert->Go();
    }

    Quit();
    return;
}

status_t App::MapRelationPropertiesToArguments(BMessage *message)
{
    status_t result;
    int32 page;

    if ((result = message->FindInt32("page" /* todo map PAGE attr! */, &page)) == B_OK) {
        message->AddInt32(PAGE_MSG_KEY, page); // BePDF
        message->RemoveData("page");
    }

    return result;
}

int main()
{
	App* app = new App();
	app->Run();

	delete app;
	return 0;
}
