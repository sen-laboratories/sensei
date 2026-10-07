/*
 * Copyright 2024-2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Application.h>

#include <sen/SenOntoCore.h>

#define LINE                "be:line"       // reuse common attribute
#define COLUMN              "be:column"     // reuse common attribute
#define SELECTION_START     sen::onto::core::attr::kTextStart
#define SELECTION_END       sen::onto::core::attr::kTextEnd
#define SELECTION_LINE_FROM sen::onto::core::attr::kLineStart
#define SELECTION_LINE_TO   sen::onto::core::attr::kLineEnd

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
