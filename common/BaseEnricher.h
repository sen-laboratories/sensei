/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025-2026 SEN Labs e.U.
 */
#pragma once

#include <Bitmap.h>
#include <Entry.h>
#include <Message.h>
#include <SupportDefs.h>
#include <Url.h>
#include <private/netservices2/HttpSession.h>

#include <sen/SenAttrInfo.h>

#include "MappingUtil.h"

using namespace BPrivate::Network;

class BaseEnricher {

public:
    BaseEnricher(const entry_ref* sourceRef, MappingUtil* mapper);
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
    // Note: std::string will not alter binary content unlike BString does.
    status_t FetchRemoteContent(const BUrl& httpUrl, std::string* resultBody);
    /** A token that is sent with every request as `Authorization: Bearer <token>` (a service that does not take a key in the address); empty: none. It is never logged. */
    void     SetBearerToken(const char* token) { fBearerToken = token != NULL ? token : ""; }
    /** the HTTP status of the last request that got an answer (0: none), e.g. 401 for a key that the service does not accept */
    int      LastHttpStatus() const { return fLastHttpStatus; }

protected:
    MappingUtil*        fMapper;

private:
    /** Is the attribute a list, and with what separator: from the attribute info of the type of the file (ontology), read once. */
    sen::ListConfig     ListConfigOf(const char* attribute);

    BHttpSession*       fHttpSession;
    int                 fLastHttpStatus;
    std::string         fBearerToken;
    entry_ref           fSourceRef;     // our own copy: the caller's one may be gone
    BMessage            fAttrInfo;
    bool                fAttrInfoRead;
};
