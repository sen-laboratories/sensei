/*
 * self-inc-first
 *
 * Copyright (C) 2014 xaizek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "IncludeFinderAction.hpp"

#include <clang/Frontend/CompilerInstance.h>

#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Lex/PPCallbacks.h>

#include <iostream>
#include <string>
#include <vector>

namespace
{

struct IncludeInfo {
    unsigned int lineNum;
    std::string  fileName;
    bool         global;
};

class IncludeFinder : private clang::PPCallbacks
{
public:
    IncludeFinder(const clang::CompilerInstance &compiler);

public:
    std::unique_ptr<clang::PPCallbacks> createPreprocessorCallbacks();

    void diagnoseAndReport();

    virtual void InclusionDirective(clang::SourceLocation HashLoc,
                                  const clang::Token &IncludeTok,
                                  clang::StringRef FileName,
                                  bool IsAngled,
                                  clang::CharSourceRange FilenameRange,
                                  clang::OptionalFileEntryRef File,
                                  clang::StringRef SearchPath,
                                  clang::StringRef RelativePath,
                                  const clang::Module *Imported,
                                  clang::SrcMgr::CharacteristicKind FileType);

private:
    const clang::CompilerInstance &compiler;
    std::string name;
    std::vector<IncludeInfo*> includes;
};

IncludeFinder::IncludeFinder(const clang::CompilerInstance &compiler)
    : compiler(compiler)
{
    const clang::FileID mainFile = compiler.getSourceManager().getMainFileID();
    name = compiler.getSourceManager().getFileEntryRefForID(mainFile)->getName();
}

std::unique_ptr<clang::PPCallbacks>
IncludeFinder::createPreprocessorCallbacks()
{
    return std::unique_ptr<PPCallbacks>(this);
}

void
IncludeFinder::InclusionDirective(clang::SourceLocation HashLoc,
                                  const clang::Token &IncludeTok,
                                  clang::StringRef FileName,
                                  bool IsAngled,
                                  clang::CharSourceRange FilenameRange,
                                  clang::OptionalFileEntryRef File,
                                  clang::StringRef SearchPath,
                                  clang::StringRef RelativePath,
                                  const clang::Module *Imported,
                                  clang::SrcMgr::CharacteristicKind FileType)
{
    const unsigned int lineNum = compiler.getSourceManager().getSpellingLineNumber(HashLoc);
    includes.push_back(new IncludeInfo{lineNum, FileName.str(), IsAngled});
}

}

void
IncludeFinderAction::ExecuteAction()
{
    IncludeFinder includeFinder(getCompilerInstance());
    getCompilerInstance().getPreprocessor().addPPCallbacks(
        includeFinder.createPreprocessorCallbacks()
    );
    // only parse a single file and don't follow dependency chain
    getCompilerInstance().getPreprocessor().getPreprocessorOpts().SingleFileParseMode = true;

    clang::PreprocessOnlyAction::ExecuteAction();

    includeFinder.diagnoseAndReport();
}

void
IncludeFinder::diagnoseAndReport()
{
    printf("*** diagnosis report:\n");
    printf("%ld includes:\n", includes.size());

    std::vector<IncludeInfo*>::iterator it;
    for (it = includes.begin(); it != includes.end(); ++it) {
        unsigned int lineNum = (*it)->lineNum;
        std::string  hdrPath = (*it)->fileName;
        bool         isGlobal = (*it)->global;

        std::cout << lineNum << ": " << hdrPath << (isGlobal ? " (global)" : "(local)") << std::endl;
    }
}
