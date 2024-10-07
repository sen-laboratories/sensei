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
#include <iostream>
#include <Path.h>
#include <cstdlib>
#include <cstring>

#include "App.h"
#include "clang-include-checker/ClangWrapper.hpp"
#include "../../Sensei.h"

const char* kApplicationSignature = "application/x-vnd.sen-labs.CodeExtractor";

App::App() : BApplication(kApplicationSignature)
{
}

App::~App()
{
}

void App::ArgvReceived(int32 argc, char ** argv) {
    if (argc < 1) {
        std::cerr << "Invalid usage, simply provide source file(s) as argument(s)." << std::endl;
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
        BAlert* alert = new BAlert("Error launching SEN Code Extractor",
            "Failed to resolve source file.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();
        return;
    }

    BMessage* reply = new BMessage(SENSEI_MESSAGE_RESULT);
    status_t result = ExtractIncludes(const_cast<const entry_ref*>(&ref), reply);

    if (result != B_OK) {
        reply->AddString("result", strerror(result));
    }

    message->PrintToStream();   //TEST
    message->SendReply(reply);
    //Quit();
}

status_t App::ExtractIncludes(const entry_ref* ref, BMessage *reply)
{
    status_t result;
    BPath inputPath(ref);

    try {
        ClangWrapper clangWrapper(inputPath.Path());
        int result = clangWrapper.run();
        printf("got result %d\n", result);

        switch(result) {
            case 0: return B_OK;
            case 1: return B_ERROR;
            case 2: return B_ENTRY_NOT_FOUND;
            default: return B_ERROR;
        }
    } catch (std::exception& e) {
        BString error("Could not analyse source code: ");
        error << e.what();
        reply->AddString("result", strerror(result));
        return B_ERROR;
    }

    return B_OK;
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
