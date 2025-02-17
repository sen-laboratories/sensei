/*
 * Copyright 2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Application.h>
#include <private/netservices2/HttpSession.h>
#include <Url.h>

using namespace BPrivate::Network;

class App : public BApplication
{
public:
                        App();
	virtual			    ~App();
	virtual void        RefsReceived(BMessage* message);
    virtual void        ArgvReceived(int32 argc, char ** argv);

    /**
     * call lookup service with params in message.
     */
    status_t            FetchBookMetadata(const entry_ref* ref, BMessage *message);

private:
    status_t            FetchRemoteContent(const BUrl& httpUrl, BString* resultBody);
    void                PrintUsage(const char* errorMsg = NULL);
    BHttpSession*       fHttpSession;
};
