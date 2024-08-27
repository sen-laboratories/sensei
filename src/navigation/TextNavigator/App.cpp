/*
 * Copyright 2024, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Alert.h>
#include <AppFileInfo.h>
#include <Errors.h>
#include <Roster.h>

#include "App.h"
#include "../../Sen.h"

const char* kApplicationSignature = "application/x-vnd.sen-labs.TextNavigator";

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
        alert->Go();

        Quit();
        return;
    }

    status_t result = MapRelationPropertiesToArguments(message);

    if (result != B_OK && result != B_NAME_NOT_FOUND) {
        BString error("Could not map launch arguments!\nReason: ");
        BAlert* alert = new BAlert("Invalid usage of SEN Relation Navigator",
            error << strerror(result), "OK not OK.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();

        Quit();
        return;
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
    int32 line;

    if ((result = message->FindInt32(LINE, &line)) == B_OK) {
        message->AddInt32("be:line", line); // StyledEdit and Pe
    } else return result;

    int32 column = -1;
    if ((result = message->FindInt32(COLUMN, &column)) == B_OK) {
        message->AddInt32("be:column", column); // StyledEdit and Pe
    }

    int32 selectLineFrom;
    if ((result = message->FindInt32(SELECTION_LINE_FROM, &selectLineFrom)) == B_OK) {
        message->AddInt32("from", selectLineFrom); // Pe
    }

    int32 selectLineTo;
    if ((result = message->FindInt32(SELECTION_LINE_TO, &selectLineTo)) == B_OK) {
        message->AddInt32("to", selectLineTo);    // Pe
    }

    int32 selectFromOffset;
    if ((result = message->FindInt32(SELECTION_OFFSET, &selectFromOffset)) == B_OK) {
        message->AddInt32("be:selection_offset", selectFromOffset);   // StyledEdit and Pe
    }

    int32 selectLen;
    if ((result = message->FindInt32(SELECTION_LENGTH, &selectLen)) == B_OK) {
        message->AddInt32("be:selection_length", selectLen);          // StyledEdit and Pe
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
