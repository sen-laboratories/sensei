/*
 * Copyright 2024, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Alert.h>
#include <Entry.h>
#include <Errors.h>
#include <Path.h>
#include <cstdlib>
#include <cstring>

#include "App.h"

const char* kApplicationSignature = "application/x-vnd.sen-labs.PdfExtractor";
static std::map<QPDFObjGen, int> page_map;

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

        return;
    }

    BMessage* reply = new BMessage('SErs');
    status_t result = ExtractPdfBookmarks(const_cast<const entry_ref*>(&ref), reply);

    if (result != B_OK) {
        reply->AddString("result", strerror(result));
    }

    reply->PrintToStream();
    message->SendReply(reply);

    return;
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
            std::vector<int> numbers;
            generate_page_map(qpdf);
            extract_bookmarks(odh.getTopLevelOutlines(), numbers);
        } else {
            return B_OK;
        }
    } catch (std::exception& e) {
        BString error("Could not extract content: ");
        error << e.what();
        reply->AddString("result", strerror(result));
        return B_ERROR;
    }

    return result;
}

void App::generate_page_map(QPDF& qpdf)
{
    QPDFPageDocumentHelper dh(qpdf);
    int n = 0;
    for (auto const& page: dh.getAllPages()) {
        page_map[page.getObjectHandle().getObjGen()] = ++n;
    }
}

void App::extract_bookmarks(std::vector<QPDFOutlineObjectHelper> outlines, std::vector<int>& numbers)
{
    numbers.push_back(0);

    for (auto& outline: outlines) {
        ++(numbers.back());
        show_bookmark_details(outline, numbers);
        extract_bookmarks(outline.getKids(), numbers);
    }

    numbers.pop_back();
}

void App::show_bookmark_details(QPDFOutlineObjectHelper outline, std::vector<int> numbers)
{
    std::string target = "unknown";
    QPDFObjectHandle dest_page = outline.getDestPage();
    if (!dest_page.isNull()) {
        QPDFObjGen og = dest_page.getObjGen();
        if (page_map.count(og)) {
            target = std::to_string(page_map[og]);
        }
    }
    std::cout << "[ -> " << target << " ] ";
    std::cout << outline.getTitle() << std::endl;
}

int main()
{
	App* app = new App();
	app->Run();
    if (app->InitCheck() != B_OK) {
        return 1;
    }
	delete app;
	return 0;
}
