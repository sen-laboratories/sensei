/*
 * Copyright 2024, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Alert.h>
#include <Errors.h>
#include <Roster.h>

#include "App.h"
#include "../../Sen.h"

const char* kApplicationSignature = "application/x-vnd.sen-labs.PdfNavigator";

App::App() : BApplication(kApplicationSignature)
{
}

App::~App()
{
}

void App::RefsReceived(BMessage *message)
{
    entry_ref ref;

    if (message->FindRef("refs", &ref) != B_OK) {
        BAlert* alert = new BAlert("Error launching SEN Relation Navigator",
            "Failed to resolve relation target.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);

        Quit();
        return;
    }

message->PrintToStream();

    status_t result;
    if (message->HasMessage(SEN_OPEN_RELATION_ARGS_KEY)) {
        BMessage argsMsg;
        result = message->FindMessage(SEN_OPEN_RELATION_ARGS_KEY, &argsMsg);
        if (result == B_OK) {
            result = MapRelationPropertiesToArguments(&argsMsg);
        }
        if (result == B_OK) {
            message->RemoveData(SEN_OPEN_RELATION_ARGS_KEY);
            message->Append(argsMsg);
        }
    } else {
        result = MapRelationPropertiesToArguments(message);
    }

message->PrintToStream();

    if (result != B_OK) {
        if (result != B_NAME_NOT_FOUND) {
            BString error("Failed to map launch arguments!\nReason: ");
            BAlert* alert = new BAlert("SEN Relation Navigator",
                error << strerror(result), "OK");
            alert->SetFlags(alert->Flags() | B_STOP_ALERT | B_CLOSE_ON_ESCAPE);

            Quit();
            return;
        } else {    // warn but continue
            BString error("Could not map launch arguments: no known parameter found!");
            BAlert* alert = new BAlert("SEN Relation Navigator",
                error << strerror(result), "OK");
            alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        }
    }

    // we need to build our own refs received message so we can send the properties with it
    entry_ref appRef;
    result = be_roster->FindApp(&ref, &appRef);
    if (result == B_OK) {
        result = be_roster->Launch(&appRef, message);
    }
    if (result != B_OK && result != B_ALREADY_RUNNING) {
            BString error("Could not launch target application: ");
            error << strerror(result);
            BAlert* alert = new BAlert("SEN Relation Navigator",
                error << strerror(result), "OK");
            alert->SetFlags(alert->Flags() | B_STOP_ALERT | B_CLOSE_ON_ESCAPE);
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
    } else return result;

    return result;
}

int main()
{
	App* app = new App();
	app->Run();

	delete app;
	return 0;
}
