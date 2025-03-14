/*
 * Copyright 2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Application.h>
#include "../BaseEnricher.h"

#define BOOK_MIME_TYPE          "entity/book"
#define AUTHOR_MIME_TYPE        "application/x-person"
#define THUMBNAIL_ATTR_NAME     "Media:Thumbnail"
#define THUMBNAIL_CREATION_TIME THUMBNAIL_ATTR_NAME ":CreationTime"

#define API_BASE_URL            "http://openlibrary.org/"
#define API_AUTHORS_URL         API_BASE_URL "authors/$id.json"
#define API_AUTHOR_IMG_URL      "https://covers.openlibrary.org/a/id/$photoId-$size.jpg"
#define API_COVER_URL           "https://covers.openlibrary.org/b/id/$coverId-$size.jpg"

#define OPENLIBRARY_API_AUTHOR_KEY  "OPENLIB:author_keys"
#define OPENLIBRARY_API_COVER_KEY   "OPENLIB:cover_key"     // also used for author photos

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
    status_t            FetchAuthor(const char* authorId, BMessage *msgResult);
    status_t            FetchCover(const char* coverId, std::string* coverImage);
    status_t            FetchPhoto(const char* photoId, std::string* photo);

    void                PrintUsage(const char* errorMsg = NULL);
    bool                fDebugMode;
    bool                fOverwrite;
    BaseEnricher*       fBaseEnricher;
};
