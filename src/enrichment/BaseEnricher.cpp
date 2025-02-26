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
#include <fs_attr.h>
#include <iostream>
#include <string.h>
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
    fMappingTable = new BMessage('SEmt');
    // by default, we provide a simple mapping for the file name as parameter "name" under the special key BEOS:NAME
    AddMapping("BEOS:NAME", "name");
}

BaseEnricher::~BaseEnricher()
{
    delete fHttpSession;
    delete fMappingTable;
}

status_t BaseEnricher::AddMapping(const char* source, const char* target, bool bidir)
{
    status_t result = fMappingTable->AddString(source, target);
    if (result == B_OK) {
        if (bidir) {
            // map in other direction, sanity check if different
            if (strlen(source) == strlen(target) || strncmp(source, target, strlen(target)) == 0) {
                printf("invalid arguments: bidirectional mapping for identical values requested! Skipping.");
            } else {
                result = fMappingTable->AddString(target, source);
            }
        }
    }
    if (result != B_OK) {
        printf("error adding mapping for %s -> %s: %s\n", source, target, strerror(result));
        return result;
    }
    return B_OK;
}

/**
* low level mapping between file system attributes and the Enricher
*/
status_t BaseEnricher::MapAttrsToMsg(const entry_ref* ref, BMessage *attrMsg)
{
    if (fMappingTable->IsEmpty()) {
        printf("no mappings defined!\n");
        return B_NOT_INITIALIZED;
    }

    status_t result;
    BNode node(ref);

    result = node.InitCheck();
	if (result != B_OK) {
        printf("failed to read input file from ref %s: %s\n", ref->name, strerror(result));
		return result;
    }

	char *attrName = new char[B_ATTR_NAME_LENGTH];
    attr_info attrInfo;
    type_code attrType;

	while (node.GetNextAttrName(attrName) == B_OK) {
        if (! fMappingTable->HasString(attrName)) {
            printf("skipping unmapped attribute '%s'.\n", attrName);
            continue;
        }

        result = node.GetAttrInfo(attrName, &attrInfo);
        if (result != B_OK) {
            printf("failed to get attribute info for attribute '%s': %s\n", attrName, strerror(result));
            return result;
        }

        char attrValue[attrInfo.size + 1];
        attrType = attrInfo.type;

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
            printf("failed to read value of attribute '%s' from file %s.\n", attrName, ref->name);
            return B_ERROR;
        }

        attrMsg->AddData(attrName, attrType, attrValue, bytesRead, false);
	}

    // always add file name as pseudo internal attribute to use if needed
    attrMsg->AddString("BEOS:NAME", ref->name);

    return B_OK;
}

/**
* low level mapping from Enricher back to file system attributes.
*/
status_t BaseEnricher::MapMsgToAttrs(const BMessage *attrMsg, entry_ref* ref, bool overwrite)
{
    status_t result;
    BNode node(ref);

    result = node.InitCheck();
	if (result != B_OK) {
        printf("failed to open input file for writing %s: %s\n", ref->name, strerror(result));
		return result;
    }

    // go through all message data and write to attributes with respective name and type from message key/type
    for (int32 i = 0; i < attrMsg->CountNames(B_ANY_TYPE); i++) {
        char *key;
        uint32 type;
        result = attrMsg->GetInfo(B_ANY_TYPE, i, &key, &type);

        if (result == B_OK) {
            const void* data;
            ssize_t dataSize;
            result = attrMsg->FindData(reinterpret_cast<const char*>(key), type, i, &data, &dataSize);

            if (result == B_OK && dataSize > 0) {
                attr_info attrInfo;
                result = node.GetAttrInfo(key, &attrInfo);
                if (result != B_ENTRY_NOT_FOUND) {  // attribute not there yet, this is OK
                    if (result != B_OK) {           // another error, not OK
                        printf("error inspecting attribute '%s' of file %s: %s\n", key, ref->name, strerror(result));
                        return result;
                    }
                } else {
                    if (! overwrite) {
                        printf("skipping existing attribute '%s' of file %s: use flag 'overwrite' to force replace.\n",
                            key, ref->name);
                    }
                }
                ssize_t attrSize = node.WriteAttr(key, type, 0, data, dataSize);
                if (attrSize < 0) {
                    printf("failed to write attribute '%s' to file %s: %s\n", key, ref->name, strerror(attrSize));
                    return attrSize;    // maps to system error if negative
                }
            }
        }
    }
    return B_OK;
}

/**
* high-level mapping from well known entity attributes to (external) service parameters.
*/
status_t BaseEnricher::MapAttrsToServiceParams(const BMessage *attrMsg, BMessage *serviceParamMsg)
{
    if (fMappingTable->IsEmpty()) {
        printf("no mappings defined!\n");
        return B_NOT_INITIALIZED;
    }

    // map all message data using mapping table for names and source types for values
    char *key;
    uint32 type;
    const void* data;
    status_t result;
    ssize_t dataSize;

    for (int32 i = 0; i < attrMsg->CountNames(B_ANY_TYPE); i++) {
        result = attrMsg->GetInfo(B_ANY_TYPE, i, &key, &type);

        if (result != B_OK) {
            printf("could not read attribute info #%d from message, skipping: %s\n", i, strerror(result));
            continue;
        }

        result = attrMsg->FindData(key, type, 0, &data, &dataSize);
        if (result != B_OK) {
            printf("could not read attribute '%s' @%d from message, skipping: %s\n", key, i, strerror(result));
            return result;
        }

        // translate key from attribute name to service parameter name from mapping table
        const char* paramName = fMappingTable->GetString(key);
        if (paramName == NULL) {
            printf("could not find parameter mapping for attribute '%s', skipping.\n", key);
            continue;
        }

        // handle collection values for Strings (separated by ",")
        if (type == B_STRING_TYPE) {
            BString valueStr(reinterpret_cast<const char*>(data), dataSize);

            if (! valueStr.IsEmpty()) {
                BStringList valueList;
                valueStr.Split(",", true, valueList);
                for (int i = 0; i < valueList.CountStrings(); i++) {
                    serviceParamMsg->AddString(paramName, valueList.StringAt(i));
                }
            } else {
                printf("ignoring empty string value for attribute '%s' [%s]\n", paramName, key);
                continue;
            }
        } else {
            // add typed data, only convert on demand later
            serviceParamMsg->AddData(paramName, type, data, dataSize, false);
        }
    }
    return B_OK;
}

/**
* map all service data from input message to attributes using mapping table for names and source types for values.
* @serviceParamMsg  the data result from a service call using service parameters as keys
* @attrMsg          an empty message to capture converted service results.
*/
status_t BaseEnricher::MapServiceParamsToAttrs(const BMessage *serviceParamMsg, BMessage *attrMsg)
{
    if (fMappingTable->IsEmpty()) {
        printf("no mappings defined!\n");
        return B_NOT_INITIALIZED;
    }
    char*       paramName;
    uint32      type;
    int32       count;
    const void* data;
    ssize_t     dataSize;
    status_t    result;

    for (int32 i = 0; i < serviceParamMsg->CountNames(B_ANY_TYPE); i++) {

        result = serviceParamMsg->GetInfo(B_ANY_TYPE, i, &paramName, &type, &count);
        if (result != B_OK) {
            printf("could not read attribute info #%d from message, skipping: %s\n", i, strerror(result));
            continue;
        }

        // translate key from attribute name to service parameter name from mapping table
        const char* key = fMappingTable->GetString(paramName);
        if (key == NULL) {
            printf("no attribute mapping defined for parameter '%s', skipping.\n", paramName);
            continue;
        }

        switch (type) {
            case B_MESSAGE_TYPE: {
                printf("message mapping not supported here, try with ConvertMessageToArray().\n");
                break;
            }
            // handle collection values for Strings (stored as list in service msg but as comma separated list in attrs)
            case B_STRING_TYPE: {
                BStringList values;
                for (int i = 0; i < count; i++) {
                    values.Add(serviceParamMsg->GetString(paramName, i, NULL));
                }
                BString value = values.Join(",", B_ATTR_NAME_LENGTH);
                attrMsg->AddString(key, value);

                break;
            }
            default: {
                result = serviceParamMsg->FindData(paramName, type, &data, &dataSize);
                if (result != B_OK) {
                    printf("could not read attribute '%s' @%d from message, skipping: %s\n", key, i, strerror(result));
                    continue;
                }
                // add typed data, only convert on demand later
                attrMsg->AddData(key, type, data, dataSize, false);
            }
        }
    }
    return B_OK;
}

status_t BaseEnricher::ConvertMessageMapsToArray(const BMessage* srcMessage, BMessage* resultMsg, BStringList* keys)
{
    status_t    result;

    char*       key;
    uint32      type;
    int32       count;
    const void* data;
    ssize_t     dataSize;

    for (int i = 0; i < srcMessage->CountNames(B_ANY_TYPE); i++) {
        result = srcMessage->GetInfo(B_ANY_TYPE, i, &key, &type, &count);
        if (result != B_OK) {
            printf("could not read src message at index %d, aborting: %s\n", i, strerror(result));
            return result;
        }

        result = srcMessage->FindData(key, type, &data, &dataSize);
        if (result != B_OK) {
            printf("could not read src message value for key %s, aborting: %s\n", key, strerror(result));
            return result;
        }

        if ((type != B_MESSAGE_TYPE) || (keys != NULL && !(keys->HasString(key))) ) {
            printf("not converting non-message attribute %s, adding as is.\n", key);

            // still add to result as is
            resultMsg->AddData(key, type, data, dataSize, false);
            continue;
        }

        BMessage valueMapMsg;
        valueMapMsg.Unflatten(reinterpret_cast<const char*>(data));

        result = ConvertSingleMessageMapToArray(&valueMapMsg, key, resultMsg);
        if (result != B_OK) {
            return result;
        }
    }

    printf("successfully converted value map msg to array values:\n");
    resultMsg->PrintToStream();

    return B_OK;
}

status_t BaseEnricher::ConvertSingleMessageMapToArray(const BMessage* msg, const char* originalKey, BMessage* resultMsg)
{
    if (msg == NULL) {
        printf("no message given!\n");
        return B_NOT_INITIALIZED;
    }

    char*       mapKey;
    uint32      type;
    const void* data;
    ssize_t     dataSize;
    int32       count;
    status_t    result;

    for (int i = 0; i < msg->CountNames(B_ANY_TYPE); i++) {
        result = msg->GetInfo(B_ANY_TYPE, i, &mapKey, &type, &count);
        if (result != B_OK) {
            printf("could not read msg info for map key at index %d, aborting: %s\n", i, strerror(result));
            return result;
        }
        // check if we can/should map the entry, i.e. key is a number
        int mapIndex;
        try {
            mapIndex = std::stoi(mapKey);
        }
        catch (std::invalid_argument const& ex)
        {
            printf("could not convert map key %s, skipping: %s\n", mapKey, ex.what());
            return B_BAD_VALUE;
        }
        catch (std::out_of_range const& ex)
        {
            printf("could not convert map key %s, skipping: %s\n", mapKey, ex.what());
            return B_BAD_VALUE;
        }

        result = msg->FindData(mapKey, type, &data, &dataSize);
        if (result != B_OK) {
            printf("could not read msg map data for key %s, aborting: %s\n", mapKey, strerror(result));
            return result;
        }

        result = resultMsg->AddData(originalKey, type, data, dataSize, false);
        if (result != B_OK) {
            printf("could not add data for key %s to result msg, aborting: %s\n", originalKey, strerror(result));
            return result;
        }
    }
    // all done successfully!
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
status_t BaseEnricher::FetchByHttpQuery(const BUrl& apiBaseUrl, BMessage *msgQuery, BMessage *msgResult)
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
            result = msgQuery->FindData(key, type, 0, &data, &dataSize);
            if (result == B_OK) {
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
                    case B_DOUBLE_TYPE:
                        value << (*(const double*) data);
                        break;
                    case B_FLOAT_TYPE:
                        value << (*(const float*) data);
                        break;
                    case B_BOOL_TYPE:
                        value << *((bool*) data);
                        break;
                    // TODO: cover remaining types!
                    default:
                        printf("unsupported type %u, skipping.\n", type);
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
    fields.AddField("User-Agent"sv, "Haiku/SEN (Senity Book Enricher)"sv);
    fields.AddField("Accept"sv, "*/*"sv);

	auto body = make_exclusive_borrow<BMallocIO>();
    BHttpStatus status;

    printf("sending HTTP request %s...\n", httpUrl.UrlString().String());

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
            printf("got HTTP result with BODY length %d\n", body->BufferLength());
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
