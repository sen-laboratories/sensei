/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025-2026 SEN Labs e.U.
 */
#include <MimeType.h>
#include <NodeInfo.h>
#include <fs_attr.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "MappingUtil.h"
#include <sen/Sen.h>
#include <sen/Sensei.h>

MappingUtil::MappingUtil()
{
    fMappingTable = new BMessage('SEmt');
}

MappingUtil::~MappingUtil()
{
    delete fMappingTable;
}

status_t MappingUtil::AddAlias(const char* source, const char* target, bool bidir)
{
    status_t result = fMappingTable->AddString(source, target);
    if (result == B_OK) {
        if (bidir) {
            // map in other direction, sanity check if different
            if (strcmp(source, target) == 0) {
                printf("invalid arguments: bidirectional mapping for identical values requested! Skipping.\n");
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

const char* MappingUtil::ResolveAlias(const char* alias, const char* defaultValue)
{
    return fMappingTable->GetString(alias, defaultValue);
}

/**
* low level mapping between file system attributes and the SEN plugins
*/
status_t MappingUtil::MapAttrsToMsg(const entry_ref* ref, BMessage *attrMsg)
{
    status_t result;
    BNode node(ref);

    result = node.InitCheck();
	if (result != B_OK) {
        printf("failed to read input file from ref %s: %s\n", ref->name, strerror(result));
		return result;
    }

	char attrName[B_ATTR_NAME_LENGTH];
    attr_info attrInfo;

    node.RewindAttrs();
	while (node.GetNextAttrName(attrName) == B_OK) {
        if (IsInternalAttr(attrName)) {
            printf("skipping internal attribute %s.\n", attrName);
            continue;
        }

        if (! fMappingTable->HasString(attrName)) {
            // usually happens when directly processing file attributes, which should already be in canonical form.
            printf("processing attribute '%s' as is, no mapping defined.\n", attrName);
        }

        result = node.GetAttrInfo(attrName, &attrInfo);
        if (result != B_OK) {
            printf("failed to get attribute info for attribute '%s': %s\n", attrName, strerror(result));
            return result;
        }

        // an empty attribute has nothing to map (and is no error)
        if (attrInfo.size == 0)
            continue;

        // (on the heap: an attribute can be large)
        std::vector<char> attrValue(attrInfo.size);

        ssize_t bytesRead = node.ReadAttr(attrName, attrInfo.type, 0, attrValue.data(), attrInfo.size);
        if (bytesRead < 0) {
            printf("failed to read value of attribute '%s' from file %s: %s\n", attrName, ref->name, strerror(bytesRead));
            return (status_t) bytesRead;
        }

        result = attrMsg->AddData(attrName, attrInfo.type, attrValue.data(), bytesRead, false);
        if (result != B_OK) {
            break;
        }
	}

    if (result != B_OK) {
        printf("error mapping attributes: %s\n", strerror(result));
        return result;
    }

    // always add file name as pseudo internal attribute to use if needed
    attrMsg->AddString(sensei::key::kName, ref->name);

    return result;
}

/**
* low level mapping from Enricher back to file system attributes.
*/
status_t MappingUtil::MapMsgToAttrs(const BMessage *attrMsg, entry_ref* targetRef, bool overwrite)
{
    status_t result;
    BNode node(targetRef);

    result = node.InitCheck();
	if (result != B_OK) {
        printf("failed to open output file '%s' for writing: %s\n", targetRef->name, strerror(result));
		return result;
    } else {
        printf("writing metadata to fs attributes of output file '%s'...\n", targetRef->name);
    }

    // go through all message data and write to attributes with respective name and type from message key/type
    for (int32 i = 0; i < attrMsg->CountNames(B_ANY_TYPE); i++) {
        char *key;
        uint32 type;
        result = attrMsg->GetInfo(B_ANY_TYPE, i, &key, &type);

        if (result == B_OK) {
            const void* data;
            ssize_t dataSize;
            result = attrMsg->FindData(key, type, &data, &dataSize);

            if (result == B_OK && dataSize > 0) {
                // check for internal file name attribute and rename file if different
                if (strncmp(key, sensei::key::kName, strlen(sensei::key::kName)) == 0) {
                	BString fileName((const char*) data, strnlen((const char*) data, dataSize));
                	fileName.Trim();
                	fileName.ReplaceAll('/', '-');	// (a name cannot hold a path)
                	if (! fileName.IsEmpty() && fileName != targetRef->name) {
                		BEntry targetEntry(targetRef);
                		// overewriting existing name is not checked again here, has to be
                		// one by specific enricher based on its settings and other logic.
                		if (targetEntry.InitCheck() == B_OK) {
                			status_t renamed = targetEntry.Rename(fileName.String());
                			if (renamed != B_OK) {
                				printf("error renaming outupt file '%s' to '%s', ignoring: %s\n",
                						targetRef->name, fileName.String(), strerror(renamed));
                			} else {
                				// the reference of the caller has to follow, or it names a file that is not there any more
                				targetEntry.GetRef(targetRef);
                			}
                		}
                	}
                	// done
                    continue;
                }

                // check if attribute is already present
                attr_info attrInfo;

                result = node.GetAttrInfo(key, &attrInfo);
                if (result != B_OK) {
                    if (result != B_ENTRY_NOT_FOUND) {
                        printf("error inspecting attribute '%s' of file %s: %s\n", key, targetRef->name, strerror(result));
                        return result;
                    }
                } else {
                    if (! overwrite) {
                        printf("skipping existing attribute '%s' of file %s: use flag 'overwrite' to force replace.\n",
                            key, targetRef->name);
                        continue;
                    }
                }
                ssize_t attrSize = node.WriteAttr(key, type, 0, data, dataSize);
                if (attrSize < 0) {
                    printf("failed to write attribute '%s' to file %s: %s\n", key, targetRef->name, strerror(attrSize));
                    return attrSize;    // maps to system error if negative
                }
            }
        }
    }
    return B_OK;
}

status_t MappingUtil::GetMimeTypeAttrs(const entry_ref* ref, BMessage *mimeAttrMsg)
{
    BNode node(ref);
    BMimeType mimeType;
    char type[B_MIME_TYPE_LENGTH];

    BNodeInfo nodeInfo(&node);
    status_t result = nodeInfo.InitCheck();
    if (result == B_OK)
        result = nodeInfo.GetType(type);

    if (result == B_OK) {
        result = mimeType.SetTo(type);
    }
    if (result != B_OK) {
        result = BMimeType::GuessMimeType(ref, &mimeType);
        if (result != B_OK) {
            printf("failed to get MIME info for input file %s: %s\n", ref->name, strerror(result));
            return result;
        }
    }

    printf("got MIME Type %s for ref '%s'.\n", mimeType.Type(), ref->name);

    BMessage attrInfoMsg;
    result = mimeType.GetAttrInfo(&attrInfoMsg);

    if (result != B_OK) {
        printf("failed to get attrInfo for MIME type %s: %s\n", mimeType.Type(), strerror(result));
        return result;
    }

    // a type without attributes has none (the count is not set when there is no field)
    int32 count = 0;
    if (attrInfoMsg.GetInfo("attr:name", NULL, &count) != B_OK)
        return B_OK;

    // fill in name and type and return as msg
    for (int32 info = 0; info < count; info++) {

        const char* attrName = attrInfoMsg.GetString("attr:name", info, NULL);
        if (attrName == NULL) {
            printf("failed to get MIME attribute info for attribute: could not get 'attr:name'!\n");
            return B_ERROR;
        }
        int32 typeCode = attrInfoMsg.GetInt32("attr:type", info, -1);
        if (typeCode < 0) {
            printf("failed to get attribute type 'attr:type' for attribute %s\n", attrName);
            return B_ERROR;
        }
        // add name/type mapping for MIME type
        mimeAttrMsg->AddInt32(attrName, typeCode);
    }

    return B_OK;
}

bool MappingUtil::IsInternalAttr(const char* attrName)
{
    BString name(attrName);

    return name.StartsWith("be:") ||
           name.StartsWith("BEOS:") ||
           name.StartsWith("META:") ||
           name.StartsWith("_trk/") ||
           name.StartsWith("Media:Thumbnail") ||
           // application specific metadata
           name.StartsWith("bepdf:") ||
           name.StartsWith("pe-info") ||
           name.StartsWith("PDF:") ||
           name.StartsWith("StyledEdit");
}
