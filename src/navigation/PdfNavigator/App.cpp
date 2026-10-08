/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */

#include <Alert.h>
#include <AppFileInfo.h>
#include <Errors.h>
#include <iostream>
#include <MimeType.h>
#include <Roster.h>
#include <String.h>

#include "App.h"
#include <sen/Sen.h>

const char* kApplicationSignature = "application/x-vnd.sen-labs.PdfNavigator";

App::App() : BApplication(kApplicationSignature)
{
    fMapper = new MappingUtil();
}

App::~App()
{
    delete fMapper;
}

int main()
{
	App* app = new App();
	app->Run();

	delete app;
	return 0;
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
        refsMsg.AddInt32(PAGE_ATTR, page);
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
        alert->SetFlags(alert->Flags() | B_STOP_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();

        Quit();
        return;
    }

    status_t result;
    BMessage argsMsg, propsMsg;

    // e.g. when coming directly from relation menu
    result = message->FindMessage(sen::key::kRelationProperties, &propsMsg);
    if (result != B_OK) {
        if (result == B_NAME_NOT_FOUND) {   // try to map from fs attributes directly (double click relation file)
            result = fMapper->MapAttrsToMsg(&ref, &propsMsg);
            if (result == B_OK) {
                // replace ref to open if there was a relation target ref
                if (propsMsg.HasRef(sen::attr::kRelationTargetRef)) {
                    result = propsMsg.FindRef(sen::attr::kRelationTargetRef, &ref);
                    if (result == B_OK) {
                        printf("got new launch ref: %s\n", ref.name);
                        // replace in original message
                        message->ReplaceRef("refs", &ref);
                    }
                }
            }
        }
    }

    if (result == B_OK) {
        result = MapRelationPropertiesToArguments(&propsMsg, &argsMsg);
    }
    if (result == B_OK) {
        message->RemoveData(sen::key::kRelationProperties);
        message->Append(argsMsg);
        printf("launch args message is:\n");
        message->PrintToStream();
    } else {
        if (result != B_NAME_NOT_FOUND) {
            BString error("Failed to map launch arguments!\nReason:\nDetail: ");
            BAlert* alert = new BAlert("SEN Relation Navigator",
                error << strerror(result), "OK");
            alert->SetFlags(alert->Flags() | B_STOP_ALERT | B_CLOSE_ON_ESCAPE);
            alert->Go();

            Quit();
            return;
        } else {    // warn but continue
            BString error("Could not map launch arguments!\nNo known parameter found.\nDetail: ");
            BAlert* alert = new BAlert("SEN Relation Navigator",
                error << strerror(result), "OK");
            alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
            alert->Go();
        }
    }

    // we need to build our own refs received message so we can send the properties with it
    entry_ref appRef;
    result = be_roster->FindApp("application/pdf", &appRef);    // it's a PDF navigator after all...

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
                        printf("got MIME type '%s' for ref '%s'\n", appSig, appRef.name);
                        // send message to running instance for a more seamless experience
                        BMessenger appMess(appSig);
                        appMess.SendMessage(message);
                    }
                }
            } else {
                printf("failed to get MIME Type for ref %s: %s\n", appRef.name, strerror(result));
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

/** The launch arguments for the PDF viewer: the place as a W3C Web Annotation target (see Toji, WebAnnotation.h):
 *    oa:hasTarget = { oa:hasSelector = { type = oa:FragmentSelector, dcterms:conformsTo = RFC 3778, rdf:value = page=N } }
 *  The page of the relation is its schema:pageStart. */
status_t App::MapRelationPropertiesToArguments(const BMessage *inputMessage, BMessage *outputMessage)
{
    int32 page;
    status_t result = inputMessage->FindInt32(PAGE_ATTR, &page);
    if (result != B_OK)
        return result;

    BString value;
    value << "page=" << page;

    BMessage selector, target;
    selector.AddString("type", "oa:FragmentSelector");
    selector.AddString("dcterms:conformsTo", "http://tools.ietf.org/rfc/rfc3778");
    selector.AddString("rdf:value", value);
    target.AddMessage("oa:hasSelector", &selector);

    return outputMessage->AddMessage(PAGE_TARGET_KEY, &target);
}
