#include <iostream>
#include <string>
#include <vector>

#include <clang/Frontend/CompilerInstance.h>
#include "IncludeFinder.hpp"

IncludeFinder* IncludeFinder::instance = NULL;
std::mutex     IncludeFinder::mutex = std::mutex();

std::unique_ptr<PPCallbacks>
IncludeFinder::createPreprocessorCallbacks()
{
    std::cout << "createPreprocessorCallbacks\n" << std::endl;
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
    const unsigned int lineNum = CompilerInstance().getSourceManager().getSpellingLineNumber(HashLoc);
    std::cout << "new include found at line " << lineNum << std::endl;
    includes.push_back(new IncludeInfo{lineNum, FileName.str(), SearchPath.str(), IsAngled});
}

void
IncludeFinder::EndOfMainFile()
{
    std::cout << "*** end of main file reached.\nfound " << includes.size() << " includes." << std::endl;

    std::vector<IncludeInfo*>::iterator it;
    for (it = includes.begin(); it != includes.end(); ++it) {
        unsigned int lineNum = (*it)->lineNum;
        std::string  hdrPath = (*it)->fileName;
        std::string  searchPath = (*it)->filePath;
        bool         isGlobal = (*it)->global;

        std::cout << lineNum << ": " << hdrPath << " from " << searchPath <<
            (isGlobal ? " (global)" : "(local)") << std::endl;
    }
}