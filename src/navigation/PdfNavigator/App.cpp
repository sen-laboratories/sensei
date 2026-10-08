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
#include <File.h>
#include <String.h>
#include <stdio.h>

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

    // the viewer of this file: the preferred application of the file or its type (Toji, BePDF,...), which says how to
    // pass the place to it
    entry_ref appRef;
    char appSig[B_MIME_TYPE_LENGTH] = "";
    status_t viewerResult = be_roster->FindApp(&ref, &appRef);
    if (viewerResult != B_OK)
        viewerResult = be_roster->FindApp("application/pdf", &appRef);
    if (viewerResult == B_OK) {
        BFile appFile(&appRef, B_READ_ONLY);
        BAppFileInfo appFileInfo(&appFile);
        if (appFile.InitCheck() != B_OK || appFileInfo.InitCheck() != B_OK || appFileInfo.GetSignature(appSig) != B_OK)
            appSig[0] = '\0';
    }
    printf("viewer of %s: %s (%s)\n", ref.name, appSig, strerror(viewerResult));

    if (result == B_OK) {
        result = MapRelationPropertiesToArguments(&propsMsg, &argsMsg, appSig);
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
    result = viewerResult;

    if (result == B_OK) {
        if (! be_roster->IsRunning(&appRef)) {
            result = be_roster->Launch(&appRef, message);
        } else if (appSig[0] != '\0') {
            // send message to running instance for a more seamless experience
            BMessenger appMess(appSig);
            result = appMess.SendMessage(message);
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

/** The launch arguments for the PDF viewer, as that viewer takes them (by its signature, case does not matter):
 *   Toji   the place as a W3C Web Annotation target (see Toji, WebAnnotation.h):
 *            oa:hasTarget = { oa:hasSelector = { type = oa:FragmentSelector, dcterms:conformsTo = RFC 3778, rdf:value = page=N } }
 *   BePDF  bepdf:page_num
 *  The page of the relation is its schema:pageStart. Another viewer just opens the file. */
status_t App::MapRelationPropertiesToArguments(const BMessage *inputMessage, BMessage *outputMessage, const char* viewer)
{
    int32 page;
    status_t result = inputMessage->FindInt32(PAGE_ATTR, &page);
    if (result != B_OK)
        return result;

    BString signature(viewer);
    signature.ToLower();

    if (signature.FindFirst("toji") >= 0) {
        BString value;
        value << "page=" << page;

        BMessage selector, target;
        selector.AddString("type", "oa:FragmentSelector");
        selector.AddString("dcterms:conformsTo", "http://tools.ietf.org/rfc/rfc3778");
        selector.AddString("rdf:value", value);
        target.AddMessage("oa:hasSelector", &selector);

        return outputMessage->AddMessage(PAGE_TARGET_KEY, &target);
    }
    if (signature.FindFirst("bepdf") >= 0)
        return outputMessage->AddInt32(PAGE_BEPDF_KEY, page);

    printf("no known way to pass the page to viewer '%s', opening the file only.\n", viewer);
    return B_OK;
}
