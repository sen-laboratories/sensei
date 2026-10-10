/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */

#pragma once

#include <Application.h>

#include <sen/SenOntoCore.h>

#include "../../common/MappingUtil.h"

#define PAGE_ATTR       sen::onto::core::attr::kPageStart
#define PAGE_TARGET_KEY "oa:hasTarget"
#define PAGE_MOTIVATION_KEY "oa:motivatedBy"
#define PAGE_BEPDF_KEY  "bepdf:page_num"

class App : public BApplication
{
public:
                        App();
	virtual			    ~App();
    virtual void        ArgvReceived(int32 argc, char ** argv);
	virtual void        RefsReceived(BMessage* message);

    /**
    * maps relation properties with canonical names as fields of the refs received message,
    * to be processed as args by the application. Only known properties are converted to supported arguments,
    * in the way that the viewer (by its signature) takes them.
    */
    status_t            MapRelationPropertiesToArguments(const BMessage *inputMessage, BMessage *outputMessage,
                                                         const char* viewer);

private:
    MappingUtil*        fMapper;
};
