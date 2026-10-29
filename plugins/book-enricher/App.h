/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025-2026 SEN Labs e.U.
 */
#pragma once

#include <Application.h>
#include <sen/SenOntoBooks.h>

#include "../../common/BaseEnricher.h"

#define BOOK_MIME_TYPE          sen::onto::books::mime::kBook
#define AUTHOR_MIME_TYPE        "application/x-person"

#define API_BASE_URL            "http://openlibrary.org/"
#define API_AUTHORS_URL         API_BASE_URL "authors/$id.json"
#define API_AUTHOR_IMG_URL      "https://covers.openlibrary.org/a/id/$photoId-$size.jpg"
#define API_COVER_URL           "https://covers.openlibrary.org/b/id/$coverId-$size.jpg"

#define OPENLIBRARY_API_AUTHOR_KEY  "OPENLIB:author_keys"
#define OPENLIBRARY_API_COVER_KEY   "OPENLIB:cover_key"     // also used for author photos

class App : public BApplication
{
public:
	virtual void	AboutRequested();
                        App();
	virtual			    ~App();
	virtual void        RefsReceived(BMessage* message);
    virtual void        ArgvReceived(int32 argc, char ** argv);

    /** the exit code of the program: 0, or 1 if something went wrong (a cancelled choice is no error) */
    int32               ExitCode() const { return fExitCode; }

    /**
     * call lookup service with params in message. If it finds more than one book, the user chooses (see ChooseBook()).
     * @return B_OK, B_CANCELED if the user did not choose, B_ENTRY_NOT_FOUND if there is no such book, or the error of the lookup
     */
    status_t            FetchBookMetadata(const entry_ref* ref, BMessage *resultMsg);

private:
    status_t            Enrich(BMessage* message, BMessage* reply);

    // query handling
    status_t            ChooseBook(const BMessage& books, const BString& wanted, int32* chosen);
    status_t            FetchAuthor(const char* authorId, BMessage *msgResult);
    status_t            FetchCover(const char* coverId, std::string* coverImage);
    status_t            FetchPhoto(const char* photoId, std::string* photo);

    void                ShowError(const char* message);
    void                PrintUsage(const char* errorMsg = NULL);

    bool                fDebugMode;
    bool                fOverwrite;
    bool                fAuto;          // take the best book without asking
    int32               fExitCode;

    BaseEnricher*       fBaseEnricher;
    MappingUtil*        fMapper;
};
