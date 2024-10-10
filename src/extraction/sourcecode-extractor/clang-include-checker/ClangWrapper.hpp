#pragma once

#include <clang/Tooling/Tooling.h>

class ClangWrapper {
	public:
		ClangWrapper(const char* filePath);
	    virtual ~ClangWrapper();
	    int run(BMessage* reply);

    private:
        const char* fSourcePath;
};
