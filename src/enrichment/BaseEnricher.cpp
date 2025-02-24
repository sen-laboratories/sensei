/*
 * Copyright 2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "BaseEnricher.h"

#include <MimeType.h>
#include <NodeInfo.h>
#include <String.h>
#include <Url.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fs_attr.h>
#include <private/netservices2/ExclusiveBorrow.h>
#include <private/netservices2/HttpFields.h>
#include <private/netservices2/HttpRequest.h>
#include <private/netservices2/HttpResult.h>
#include <private/netservices2/NetServicesDefs.h>
#include <private/shared/Json.h>

using namespace BPrivate::Network;
using namespace std::literals;

BaseEnricher::BaseEnricher()
{
    fHttpSession = new BHttpSession();
}

BaseEnricher::~BaseEnricher()
{
    delete fHttpSession;
}

status_t BaseEnricher::MapAttrsToMsg(const entry_ref* ref, BMessage *attrMsg)
{
    status_t result;
    BNode node(ref);

    result = node.InitCheck();
	if (result != B_OK) {
        printf("failed to read input file from ref %s: %s\n", ref->name, strerror(result));
		return result;
    }

	char *attrName = new char[B_ATTR_NAME_LENGTH];
    BMessage mimeInfoMsg;

    result = GetMimeTypeAttrs(ref, &mimeInfoMsg);
    if (result != B_OK) {
        return result;
    }

    // always add file name as fallback for title
    attrMsg->AddString("name", ref->name);

    attr_info attrInfo;
	while (node.GetNextAttrName(attrName) == B_OK) {
        int32 attrType = mimeInfoMsg.GetInt32(attrName, -1);
		if (attrType < 0) {
            // skip internal/custom attributes
            printf("skipping internal/custom attribute %s.\n", attrName);
            continue;
        }

        node.GetAttrInfo(attrName, &attrInfo);
        char attrValue[attrInfo.size + 1];

        ssize_t bytesRead = node.ReadAttr(
            attrName,
            attrType,
            0,
            attrValue,
            attrInfo.size);

        if (bytesRead == 0) {
            printf("attribute %s has unexpeted type %u in file %s.\n", attrName, attrType, ref->name);
            return B_ERROR;
        } else if (bytesRead < 0) {
            printf("failed to read attribute value for attribute %s of file %s.\n", attrName, ref->name);
            return B_ERROR;
        }

        attrMsg->AddData(attrName, attrType, attrValue, bytesRead);
	}

    return B_OK;
}

status_t BaseEnricher::GetMimeTypeAttrs(const entry_ref* ref, BMessage *mimeAttrMsg)
{
    BNode node(ref);
    BMimeType mimeType;
    char type[B_MIME_TYPE_LENGTH];

    BNodeInfo nodeInfo(&node);
    status_t result = nodeInfo.GetType(type);

    if (result == B_OK) {
        mimeType.SetType(type);
    } else {
        result = BMimeType::GuessMimeType(ref, &mimeType);
        if (result != B_OK) {
            printf("failed to get MIME info for input file %s: %s\n", ref->name, strerror(result));
            return result;
        }
    }

    BMessage attrInfoMsg;
    result = mimeType.GetAttrInfo(&attrInfoMsg);
    if (result != B_OK) {
        printf("failed to get attrInfo for MIME type %s: %s\n", mimeType.Type(), strerror(result));
        return result;
    }

    // fill in name and type and return as msg
    for (int32 info = 0; info < attrInfoMsg.CountNames(B_STRING_TYPE); info++) {
        const char* attrName = attrInfoMsg.GetString("attr:name", info, NULL);
        if (attrName == NULL) {
            printf("MIME type not in MIME DB: could not get 'attr:name'! Aborting.\n");
            return B_ERROR;
        }
        int32 typeCode = attrInfoMsg.GetInt32("attr:type", info, B_STRING_TYPE);

        // add name/type mapping for MIME type
        mimeAttrMsg->AddInt32(attrName, typeCode);
    }
    return B_OK;
}

bool BaseEnricher::IsInternalAttr(BString* attrName)
{
    return attrName->StartsWith("be:") ||
           attrName->StartsWith("BEOS:") ||
           attrName->StartsWith("META:") ||
           attrName->StartsWith("_trk/") ||
           // application specific metadata
           attrName->StartsWith("pe-info") ||
           attrName->StartsWith("PDF:") ||
           attrName->StartsWith("bepdf:");
}

// HTTP query support
status_t BaseEnricher::FetchByQuery(const BUrl& apiBaseUrl, BMessage *msgQuery, BMessage *msgResult)
{
    BString request;
    status_t result;
    BUrl queryUrl(apiBaseUrl);

    // add all message data to Url as request params
    for (int32 i = 0; i < msgQuery->CountNames(B_ANY_TYPE); i++) {
        char *key;
        uint32 type;
        result = msgQuery->GetInfo(B_ANY_TYPE, i, &key, &type);

        if (result == B_OK) {
            const void* data;
            ssize_t dataSize;
            result = msgQuery->FindData(reinterpret_cast<const char*>(key), type, i, &data, &dataSize);
            if (result == B_OK && dataSize > 0) {
                if (! request.IsEmpty()) {
                    request << "&";
                }
                BString value;
                switch(type){
                    case B_STRING_TYPE:
                        value << (const char*) data;
                        break;
                    case B_INT32_TYPE:
                        value << (*(const int32*) data);
                        break;
                    case B_BOOL_TYPE:
                        value << *((bool*) data);
                        break;
                    // TODO: cover remaining types!
                    default:
                        printf("unsupported type, skipping.");
                        break;
                }
                request << key << "=" << BUrl::UrlEncode(value);
            } else {
                printf("failed to fetch message data for key '%s': %s\n", key, strerror(result));
            }
        }
    }
    queryUrl.SetRequest(request);

    std::string resultBody;
    result = FetchRemoteContent(queryUrl, &resultBody);

    if (result != B_OK) {
        printf("error accessing remote API: %s", strerror(result));
        return result;
    }

    return BJson::Parse(resultBody.c_str(), *msgResult);
}

status_t
BaseEnricher::FetchRemoteContent(const BUrl& httpUrl, std::string* resultBody)
{
    auto request = BHttpRequest(httpUrl);
    request.SetTimeout(3000 /*ms*/);

    auto fields = request.Fields();
    fields.AddField("User-Agent"sv, "Haiku/SEN"sv);
    fields.AddField("Accept"sv, "*/*"sv);

	auto body = make_exclusive_borrow<BMallocIO>();
    BHttpStatus status;

    try {
        auto result = fHttpSession->Execute(std::move(request), BBorrow<BDataIO>(body));
        // the Status() call will block until full response has been received
        status = result.Status();
        result.Body();  // synchronize with BBorrow buffer (see HttpSession::Execute docs)
    } catch (const BPrivate::Network::BNetworkRequestError& err) {
        return err.ErrorCode();
    }
    if (status.code >= 200 && status.code <= 400) {
        try {
            std::string bodyContent = std::string(reinterpret_cast<const char*>(body->Buffer()), body->BufferLength());
            bool hasBody = ! bodyContent.empty();
            *resultBody = bodyContent;
         } catch (const BPrivate::Network::BBorrowError& err) {
            std::cout << "  BorrowError: " << err.Message() << ", origin: " << err.Origin() << std::endl;
            return B_ERROR;
         }
    } else {
        std::cout << "  HTTP error " << status.code
                  << " reading from URL " << httpUrl << " - "
                  << status.text << std::endl;
        return B_ERROR;
    }
    return B_OK;
}
