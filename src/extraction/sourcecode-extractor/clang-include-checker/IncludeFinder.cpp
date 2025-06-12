#include <iostream>
#include <vector>

#include <clang/Frontend/CompilerInstance.h>

#include "IncludeFinder.hpp"

std::unique_ptr<PPCallbacks>
IncludeFinder::createPreprocessorCallbacks()
{
    std::cout << "createPreprocessorCallbacks\n";
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
                                  const Module *SuggestedModule,
                                  bool ModuleImported,
                                  SrcMgr::CharacteristicKind FileType)
{
    const unsigned int lineNum = compiler->getSourceManager().getSpellingLineNumber(HashLoc);

	std::cout << "adding include: file " << FileName.str() << " with line " << lineNum
              << " and path " << SearchPath.str() << std::endl;

    includes.push_back(new IncludeInfo{lineNum, FileName.str(), SearchPath.str(), IsAngled});
}

void
IncludeFinder::EndOfMainFile()
{
    std::cout << "*** end of main file reached, found " << includes.size() << " includes." << std::endl;
}
