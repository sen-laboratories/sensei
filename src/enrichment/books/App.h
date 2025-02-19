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
    status_t            FetchBookMetadata(const entry_ref* ref, BMessage *resultMsg);

private:
    // attribute handling
    status_t            MapAttrsToMsg(const entry_ref* ref, BMessage *attrMsg);
    status_t            GetMimeTypeAttrs(const entry_ref* ref, BMessage *mimeAttrMsg);
    bool                IsInternalAttr(BString* attrName);

    // query handling
    status_t            FetchAuthor(BMessage *msgQuery, BMessage *msgResult);
    status_t            FetchByQuery(BMessage *msgQuery, BMessage *msgResult);
    status_t            FetchCover(BMessage *msgQuery, BMessage *msgResult);

    // remote connection handling
    status_t            FetchRemoteContent(const BUrl& httpUrl, BString* resultBody);
    void                PrintUsage(const char* errorMsg = NULL);
    BHttpSession*       fHttpSession;
    bool                fDebugMode;
};
