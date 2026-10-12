/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */
#pragma once

#include <Application.h>
#include <qpdf/QIntC.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QTC.hh>
#include <qpdf/QUtil.hh>

#define PAGE    "page"

class App : public BApplication
{
public:
                        App();
	virtual			    ~App();
	virtual void        RefsReceived(BMessage* message);
    virtual void        ArgvReceived(int32 argc, char ** argv);

    /**
    * source file to extract content from.
    */
    status_t            ExtractPdfBookmarks(const entry_ref* ref, BMessage *message);

private:
    void GeneratePageMap(QPDF& qpdf);
    void ExtractBookmarks(std::vector<QPDFOutlineObjectHelper> outlines, BMessage *msg);
    BMessage* AddBookmarkDetails(QPDFOutlineObjectHelper outline, BMessage *msg);
};
