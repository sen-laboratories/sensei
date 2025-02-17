/*
 * Copyright 2024, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef APP_H
#define APP_H

#include <Application.h>
#include <qpdf/QIntC.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QTC.hh>
#include <qpdf/QUtil.hh>

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

#endif // APP_H
