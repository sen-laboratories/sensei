/*
 * Copyright 2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Bitmap.h>
#include <Entry.h>
#include <Message.h>
#include <SupportDefs.h>

class MappingUtil {

public:
    MappingUtil();
    virtual ~MappingUtil();

    status_t AddAlias(const char* source, const char* target, bool bidir = true);

    const char* ResolveAlias(const char* alias, char* defaultValue = NULL);
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

    static status_t GetMimeTypeAttrs(const entry_ref* ref, BMessage *mimeAttrMsg);
    static bool IsInternalAttr(const char* attrName);

private:
    BMessage*    fMappingTable;
};
