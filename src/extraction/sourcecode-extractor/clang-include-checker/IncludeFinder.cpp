#include <iostream>
#include <vector>

#include <clang/Frontend/CompilerInstance.h>
#include "IncludeFinder.hpp"

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

	#ifdef DEBUG
    printf("adding include: file %s with line %d and path %s\n",
        FileName.str().c_str(), lineNum, SearchPath.str().c_str());
	#endif
    includes.push_back(new IncludeInfo{lineNum, FileName.str(), SearchPath.str(), IsAngled});
}

void
IncludeFinder::EndOfMainFile()
{
    std::cout << "*** end of main file reached, found " << includes.size() << " includes." << std::endl;
}
