/*
 * Copyright 2025, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Application.h>

class App : public BApplication
{
public:
                        App();
    virtual            ~App();
    virtual void        RefsReceived(BMessage* message);
    virtual void        ArgvReceived(int32 argc, char ** argv);

    status_t            ExtractIncludes(const entry_ref* ref, BMessage *message);

private:
};
