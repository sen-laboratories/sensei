/*
 * Copyright 2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Application.h>
#include "../BaseEnricher.h"

#define BOOK_MIME_TYPE          "entity/book"
#define THUMBNAIL_ATTR_NAME     "Media:Thumbnail"
#define THUMBNAIL_CREATION_TIME THUMBNAIL_ATTR_NAME ":CreationTime"

#define API_BASE_URL            "http://openlibrary.org/"
#define API_AUTHORS_URL         API_BASE_URL "authors/"
#define API_AUTHOR_IMG_URL      "https://covers.openlibrary.org/a/olid/$olid-$size.jpg"
#define API_COVER_URL           "https://covers.openlibrary.org/b/$key/$value-$size.jpg"

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
    status_t            FetchCover(const char* coverId, BBitmap* coverImage, size_t* imageSize);

    void                PrintUsage(const char* errorMsg = NULL);
    bool                fDebugMode;
    bool                fOverwrite;
    BaseEnricher*       fBaseEnricher;
};
