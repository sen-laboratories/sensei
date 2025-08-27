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

#include "../common/MappingUtil.h"

using namespace BPrivate::Network;

class BaseEnricher {

public:
    BaseEnricher(entry_ref* sourceRef, MappingUtil* mapper);
    virtual ~BaseEnricher();

    /*
    * high level mapping
    */
    status_t MapAttrsToServiceParams(const BMessage *attrMsg, BMessage *serviceParamMsg);
    status_t MapServiceParamsToAttrs(const BMessage *serviceParamMsg, BMessage *attrMsg);

    /*
    * conversion
    */
    static status_t ConvertMessageMapsToArray(const BMessage* mapMessage, BMessage* resultMsg, BStringList* keys = NULL);
    static status_t ConvertSingleMessageMapToArray(const BMessage* msg, const char* originalKey, BMessage* resultMsg);

    status_t CreateHttpApiUrl(const char* apiUrlPattern, const BMessage* apiParamMapping, BUrl* resultUrl);
    // these need a valid HTTP session and are bound to the lifecycle of this class
    status_t FetchRemoteJson(const BUrl& httpUrl, BMessage& jsonMsgResult);
    status_t FetchByHttpQuery(const BUrl& apiBaseUrl, BMessage* msgQuery, BMessage* msgResult);
    status_t FetchRemoteImage(const BUrl& httpUrl, BBitmap* resultImage, size_t* imageSize);
    // Note: std::string will not alter binary content unlike BString does.
    status_t FetchRemoteContent(const BUrl& httpUrl, std::string* resultBody);

protected:
    MappingUtil*        fMapper;

private:
    BHttpSession*       fHttpSession;
    entry_ref*          fSourceRef;
};
