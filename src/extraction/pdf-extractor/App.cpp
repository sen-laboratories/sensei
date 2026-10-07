/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
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

    BMessage reply(sensei::cmd::kResult);
    status_t result = ExtractPdfBookmarks(const_cast<const entry_ref*>(&ref), &reply);
    reply.AddInt32(sensei::key::kResult, result);
    reply.AddString(sen::key::kDetail, strerror(result));

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
    BMessage childrenRoot(sensei::cmd::kResult);

    for (auto& outline: outlines) {
        AddBookmarkDetails(outline, &childrenRoot);
        // recurse with bookmark just added as new parent node
        ExtractBookmarks(outline.getKids(), &childrenRoot);
    }

    // Note: we also add empty subnodes here to keep the structure intact
    msg->AddMessage(sensei::key::kItem, &childrenRoot);
    msg->AddString (sensei::key::kItemId, "");           // filled in by SEN, just indicate we need a unique ID here
    msg->AddString (sensei::key::kTo, sensei::to::kSelf);    // target is always self for bookmarks
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
    msg->AddString(sensei::key::kLabel, outline.getTitle().c_str());
    // specific docref attributes - uses aliases for full attribute names defined in plugin config map
    msg->AddInt32(PAGE, targetPage);

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
