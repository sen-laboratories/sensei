/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025-2026 SEN Labs e.U.
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

    status_t            ExtractIncludes(const entry_ref* ref, bool self, BMessage *message);

private:
};
