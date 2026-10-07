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
#include <sen/Sen.h>
#include <sen/Sensei.h>

const char* kApplicationSignature = "application/x-vnd.sen-labs.SourceCodeExtractor";

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
    int32 arg = 1;

    // parse optional "self" param
    if (strncmp(argv[arg], sensei::kOptionSelf, strlen(sensei::kOptionSelf)) == 0) {
        refsMsg.AddBool(sen::conf::kSelf, true);
        arg++;
    }

    BEntry entry(argv[arg]);
    entry_ref ref;
    entry.GetRef(&ref);
    refsMsg.AddRef("refs", &ref);

    RefsReceived(&refsMsg);
}

void App::RefsReceived(BMessage *message)
{
    entry_ref ref;

    if (message->FindRef("refs", &ref) != B_OK) {
        BAlert* alert = new BAlert("Error launching SEN SourceCode Extractor",
            "Failed to resolve source file.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();
        return;
    }

    bool self = message->GetBool(sen::conf::kSelf, false);

    BMessage reply(sensei::cmd::kResult);
    status_t result = ExtractIncludes(const_cast<const entry_ref*>(&ref), self, &reply);

    // the result of the plugin is an int32 status_t (the server reads it), with a text for people and logs
    reply.AddInt32(sensei::key::kResult, result);
    reply.AddString(sen::key::kDetail, strerror(result));   // TODO: handle includes not found correctly

    //TEST
    std::cout << "sending reply:\n";
    reply.PrintToStream();

    // we don't expect a reply but run into a race condition with the app
    // being deleted too early, resulting in a malloc assertion failure.
    message->SendReply(&reply, this);
    Quit();
}

status_t App::ExtractIncludes(const entry_ref* ref, bool self, BMessage *reply)
{
    status_t result;
    BPath inputPath(ref);

    std::cout << "extracting " << (self ? "self " : "") << "include refs from " << ref->name << "...\n";

    try {
        ClangWrapper clangWrapper(inputPath.Path(), self);
        int result = clangWrapper.run(reply);

        switch(result) {
            case 0: return B_OK;
            case 1: return B_ERROR;
            case 2: return B_ENTRY_NOT_FOUND;
            default: return B_ERROR;
        }
    } catch (std::exception& e) {
        BString error("Could not analyse source code: ");
        error << e.what();
        return B_ERROR;
    }

    return B_OK;
}

int main()
{
	App app;
    if (app.InitCheck() != B_OK) {
        return 1;
    }
	app.Run();
	return 0;
}
