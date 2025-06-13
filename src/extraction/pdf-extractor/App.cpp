/*
 * Copyright 2024, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 *
 * derived from QPDF example 'pdf-bookmarks.cc'
 * see project site at https://qpdf.sourceforge.io
 */

#include <Alert.h>
#include <Entry.h>
#include <Errors.h>
#include <Path.h>
#include <cstdlib>
#include <cstring>

#include "App.h"
#include <sen/Sen.h>
#include <sen/Sensei.h>

const char* kApplicationSignature = "application/x-vnd.sen-labs.PdfExtractor";
static std::map<QPDFObjGen, int32> page_map;

App::App() : BApplication(kApplicationSignature)
{
}

App::~App()
{
}

void App::ArgvReceived(int32 argc, char ** argv) {
    if (argc < 1) {
        std::cerr << "Invalid usage, simply provide PDF file as 1st and only argument." << std::endl;
        return;
    }

    BMessage refsMsg(B_REFS_RECEIVED);
    BEntry entry(argv[1]);
    entry_ref ref;

    entry.GetRef(&ref);
    refsMsg.AddRef("refs", &ref);

    RefsReceived(&refsMsg);
}

void App::RefsReceived(BMessage *message)
{
    entry_ref ref;

    if (message->FindRef("refs", &ref) != B_OK) {
        BAlert* alert = new BAlert("Error launching SEN PDF Extractor",
            "Failed to resolve source file.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();
        return;
    }

    BMessage reply(SENSEI_MESSAGE_RESULT);
    status_t result = ExtractPdfBookmarks(const_cast<const entry_ref*>(&ref), &reply);
    reply.AddString("result", strerror(result));

    // we don't expect a reply but run into a race condition with the app
    // being deleted too early, resulting in a malloc assertion failure.
    message->SendReply(&reply, this);
    Quit();
}

status_t App::ExtractPdfBookmarks(const entry_ref* ref, BMessage *reply)
{
    status_t result;
    BPath inputPath(ref);

    try {
        QPDF qpdf;
        qpdf.processFile(inputPath.Path());
        QPDFOutlineDocumentHelper odh(qpdf);

        if (odh.hasOutlines()) {
            GeneratePageMap(qpdf);
            ExtractBookmarks(odh.getTopLevelOutlines(), reply);
        } else {
            return B_OK;
        }
    } catch (std::exception& e) {
        reply->AddString("error", e.what());
        return B_ERROR;
    }

    return B_OK;
}

void App::GeneratePageMap(QPDF& qpdf)
{
    QPDFPageDocumentHelper dh(qpdf);
    int n = 0;
    for (auto const& page: dh.getAllPages()) {
        page_map[page.getObjectHandle().getObjGen()] = ++n;
    }
}

void App::ExtractBookmarks(std::vector<QPDFOutlineObjectHelper> outlines, BMessage* msg)
{
    BMessage childrenRoot('Bmrk');

    for (auto& outline: outlines) {
        AddBookmarkDetails(outline, &childrenRoot);
        // recurse with bookmark just added as new parent node
        ExtractBookmarks(outline.getKids(), &childrenRoot);
    }

    // childrenRoot may be empty but we need to add it nevertheless as a filler so the message field order
    // (array indices) stay intact and we can relate children to their root node in the message structure.
    msg->AddMessage("item", new BMessage(childrenRoot));
}

BMessage* App::AddBookmarkDetails(QPDFOutlineObjectHelper outline, BMessage* msg)
{
    int32 targetPage = 0;
    QPDFObjectHandle dest_page = outline.getDestPage();
    if (dest_page.getObjectPtr() != NULL) {
        if (page_map.contains(dest_page.getObjGen())) {
            targetPage = page_map[dest_page.getObjGen()];
        }
    }
    // common relation attributes
    msg->AddString("label", outline.getTitle().c_str());
    // specific docref attributes - uses aliases for full attribute names defined in plugin config map
    msg->AddInt32("page", targetPage);

    return msg;
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
