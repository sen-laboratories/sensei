/*
 * Copyright 2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Application.h>
#include "../BaseEnricher.h"

#define BOOK_MIME_TYPE "entity/book"

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
    // query handling
    status_t            FetchAuthor(BMessage *msgQuery, BMessage *msgResult);
    status_t            FetchCover(BMessage *msgQuery, BMessage *msgResult);

    void                PrintUsage(const char* errorMsg = NULL);
    bool                fDebugMode;
    bool                fOverwrite;
    BaseEnricher*       fBaseEnricher;
};
