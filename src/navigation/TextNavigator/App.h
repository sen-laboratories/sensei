/*
 * Copyright 2024-2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Application.h>

#define LINE                "SEN:REL:textref:line"
#define COLUMN              "SEN:REL:textref:column"
#define SELECTION_OFFSET    "SEN:REL:textref:selection_offset"
#define SELECTION_LENGTH    "SEN:REL:textref:selection_length"
#define SELECTION_LINE_FROM "SEN:REL:textref:selection_line_from"
#define SELECTION_LINE_TO   "SEN:REL:textref:selection_line_to"

class App : public BApplication
{
public:
                        App();
	virtual			    ~App();
	virtual void        RefsReceived(BMessage* message);

    /**
    * we transparently get any relation properties as fields of the refs received message.
    */
    status_t            MapRelationPropertiesToArguments(BMessage *message);
};
