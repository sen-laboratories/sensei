/*
 * Copyright 2024, My Name <my@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef APP_H
#define APP_H

#include <Application.h>

#define PAGE                "SEN:REL:docref:page"
#define PAGE_MSG_KEY        "bepdf:page_num"

class App : public BApplication
{
public:
                        App();
	virtual			    ~App();
    virtual void        ArgvReceived(int32 argc, char ** argv);
	virtual void        RefsReceived(BMessage* message);

    /**
    * we transparently get any relation properties as fields of the refs received message.
    */
    status_t            MapRelationPropertiesToArguments(BMessage *message);

private:
};

#endif // APP_H
