/*
 * Copyright 2024-2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#pragma once

#include <Application.h>

#define PAGE_ATTR       "SEN:REL:docref:page"
#define PAGE_MSG_KEY    "bepdf:page_num"

class App : public BApplication
{
public:
                        App();
	virtual			    ~App();
    virtual void        ArgvReceived(int32 argc, char ** argv);
	virtual void        RefsReceived(BMessage* message);

    /**
    * maps relation properties with canonical names as fields of the refs received message,
    * to be processed as args by the application.
    */
    status_t            MapRelationPropertiesToArguments(BMessage *message);
};
