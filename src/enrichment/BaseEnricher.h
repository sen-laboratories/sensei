/*
 * Copyright 2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Bitmap.h>
#include <Entry.h>
#include <Message.h>
#include <SupportDefs.h>
#include <Url.h>
#include <private/netservices2/HttpSession.h>

using namespace BPrivate::Network;

#define SENSEI_NAME_ATTR "SENSEI:NAME"

class BaseEnricher {

public:
    BaseEnricher(entry_ref* sourceRef);
    virtual ~BaseEnricher();

    /*
    * high level mapping
    */
    status_t AddMapping(const char* source, const char* target, bool bidir = true);
    status_t MapAttrsToServiceParams(const BMessage *attrMsg, BMessage *serviceParamMsg);
    status_t MapServiceParamsToAttrs(const BMessage *serviceParamMsg, BMessage *attrMsg);
    /**
    * reads fs attributes with an associated mapping from the file @ref into @attrMsg,
    * using attribute names as keys.
    */
    status_t MapAttrsToMsg(const entry_ref* ref, BMessage *attrMsg);
    /**
    * writes message data from @attrMsg into attributes of file referenced by @ref
    * with respective types, using message keys as attribute names.
    * Optionally overwrites existing attributes.
    */
    status_t MapMsgToAttrs(const BMessage* attrMsg, entry_ref* targetRef, bool overwrite = false);

    /*
    * conversion
    */
    static status_t ConvertMessageMapsToArray(const BMessage* mapMessage, BMessage* resultMsg, BStringList* keys = NULL);
    static status_t ConvertSingleMessageMapToArray(const BMessage* msg, const char* originalKey, BMessage* resultMsg);

    static status_t GetMimeTypeAttrs(const entry_ref* ref, BMessage *mimeAttrMsg);
    static bool IsInternalAttr(BString* attrName);

    status_t CreateHttpApiUrl(const char* apiUrlPattern, const BMessage* apiParamMapping, BUrl* resultUrl);
    // these need a valid HTTP session and are bound to the lifecycle of this class
    status_t FetchRemoteJson(const BUrl& httpUrl, BMessage& jsonMsgResult);
    status_t FetchByHttpQuery(const BUrl& apiBaseUrl, BMessage* msgQuery, BMessage* msgResult);
    status_t FetchRemoteImage(const BUrl& httpUrl, BBitmap* resultImage, size_t* imageSize);
    // Note: std::string will not alter binary content unlike BString does.
    status_t FetchRemoteContent(const BUrl& httpUrl, std::string* resultBody);

private:
    BHttpSession*       fHttpSession;
    BMessage*           fMappingTable;
    entry_ref*          fSourceRef;
};
