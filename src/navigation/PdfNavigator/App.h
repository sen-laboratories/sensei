/*
 * Copyright 2024-2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#pragma once

#include <Application.h>

#include <sen/SenOntoCore.h>

#include "../../common/MappingUtil.h"

#define PAGE_ATTR       sen::onto::core::attr::kPageStart
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
    * to be processed as args by the application. Only known properties are converted to supported arguments.
    */
    status_t            MapRelationPropertiesToArguments(const BMessage *inputMessage, BMessage *outputMessage);

private:
    MappingUtil*        fMapper;
};
