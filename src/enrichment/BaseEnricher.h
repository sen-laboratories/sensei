/*
 * Copyright 2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Entry.h>
#include <Message.h>
#include <SupportDefs.h>
#include <Url.h>
#include <private/netservices2/HttpSession.h>

using namespace BPrivate::Network;

class BaseEnricher {

public:
    BaseEnricher();
    virtual ~BaseEnricher();

    static status_t MapAttrsToMsg(const entry_ref* ref, BMessage *attrMsg);
    static status_t GetMimeTypeAttrs(const entry_ref* ref, BMessage *mimeAttrMsg);
    static bool IsInternalAttr(BString* attrName);

    // these need a valid HTTP session and are bound to the lifecycle of this class
    status_t FetchByQuery(const BUrl& apiBaseUrl, BMessage *msgQuery, BMessage *msgResult);
    // Note: std::string will not alter binary content unlike BString does.
    status_t FetchRemoteContent(const BUrl& httpUrl, std::string* resultBody);

private:
    BHttpSession*       fHttpSession;
};
