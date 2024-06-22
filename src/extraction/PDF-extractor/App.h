/*
 * Copyright 2024, My Name <my@email.address>
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
    void generate_page_map(QPDF& qpdf);
    void extract_bookmarks(std::vector<QPDFOutlineObjectHelper> outlines, std::vector<int>& numbers);
    void show_bookmark_details(QPDFOutlineObjectHelper outline, std::vector<int> numbers);
};

#endif // APP_H
