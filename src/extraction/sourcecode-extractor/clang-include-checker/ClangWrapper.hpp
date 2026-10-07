/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */

#pragma once

#include <clang/Tooling/Tooling.h>

class ClangWrapper {
	public:
		ClangWrapper(const char* filePath, bool self = true);
	    virtual ~ClangWrapper();
	    int run(BMessage* reply);

    private:
        const char* fSourcePath;
        bool        fSelf;
};
