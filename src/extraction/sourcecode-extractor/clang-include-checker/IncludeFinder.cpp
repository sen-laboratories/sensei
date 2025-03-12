#include <iostream>
#include <vector>

#include <clang/Frontend/CompilerInstance.h>
#include "IncludeFinder.hpp"

IncludeFinder::IncludeFinder() {
    includes = new std::vector<IncludeInfo*>();
}

IncludeFinder::~IncludeFinder() {
    printf("IncludeFinder d'tor.\n");

    if (includes != NULL) {
        for (auto includeRef : *includes) {
            if (includeRef) delete includeRef;
        }
        delete includes;
    }
}

std::unique_ptr<PPCallbacks>
IncludeFinder::createPreprocessorCallbacks()
{
    return std::unique_ptr<PPCallbacks>(this);
}

void
IncludeFinder::InclusionDirective(SourceLocation HashLoc,
                                  const Token &IncludeTok,
                                  StringRef FileName,
                                  bool IsAngled,
                                  CharSourceRange FilenameRange,
                                  OptionalFileEntryRef File,
                                  StringRef SearchPath,
                                  StringRef RelativePath,
                                  const Module *Imported,
                                  SrcMgr::CharacteristicKind FileType)
{
    const unsigned int lineNum = compiler->getSourceManager().getSpellingLineNumber(HashLoc);
    includes->push_back(new IncludeInfo{lineNum, FileName.str(), SearchPath.str(), IsAngled});
}

void
IncludeFinder::EndOfMainFile()
{
    std::cout << "*** end of main file reached, found " << includes->size() << " includes." << std::endl;
}
