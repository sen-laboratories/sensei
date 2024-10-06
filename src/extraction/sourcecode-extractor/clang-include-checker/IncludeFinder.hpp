#pragma once

#include <clang/Tooling/Tooling.h>

class IncludeFinder {
	public:
		IncludeFinder(const char* filePath);
	    virtual ~IncludeFinder();
	    int run();

    private:
        const char*                fSourcePath;
};
