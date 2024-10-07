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
#include <utility>

namespace
{
/*
class CallbacksProxy : public clang::PPCallbacks
{
public:
    inline CallbacksProxy(clang::PPCallbacks &master);

public:
  virtual inline void InclusionDirective(clang::SourceLocation hashLoc,
                                  const clang::Token &includeTok,
                                  clang::StringRef fileName,
                                  bool isAngled,
                                  clang::CharSourceRange filenameRange,
                                  clang::OptionalFileEntryRef file,
                                  clang::StringRef searchPath,
                                  clang::StringRef relativePath,
                                  const clang::Module *suggestedModule,
                                  clang::SrcMgr::CharacteristicKind fileType);

private:
    clang::PPCallbacks &master;
};

inline
CallbacksProxy::CallbacksProxy(clang::PPCallbacks &master)
    : master(master)
{
}

inline void
CallbacksProxy::InclusionDirective(clang::SourceLocation hashLoc,
                                  const clang::Token &includeToken,
                                  clang::StringRef fileName,
                                  bool isAngled,
                                  clang::CharSourceRange filenameRange,
                                  clang::OptionalFileEntryRef file,
                                  clang::StringRef searchPath,
                                  clang::StringRef relativePath,
                                  const clang::Module *suggestedModule,
                                  clang::SrcMgr::CharacteristicKind fileType)
{
    master.InclusionDirective(hashLoc,
                              includeToken,
                              fileName,
                              isAngled,
                              filenameRange,
                              file,
                              searchPath,
                              relativePath,
                              suggestedModule,
                              fileType);
}
*/
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

    typedef std::pair<int, std::string> IncludeInfo;
    typedef std::vector<IncludeInfo> Includes;
    Includes includes;
};

IncludeFinder::IncludeFinder(const clang::CompilerInstance &compiler)
    : compiler(compiler)
{
    const clang::FileID mainFile = compiler.getSourceManager().getMainFileID();
    name = compiler.getSourceManager().getFileEntryRefForID(mainFile)->getName();
    printf("got file name %s\n", name.data());
}

std::unique_ptr<clang::PPCallbacks>
IncludeFinder::createPreprocessorCallbacks()
{
    printf("createPreprocessorCallbacks\n");
    return std::unique_ptr<PPCallbacks>(this); // no CallbacksProxy
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
    std::cout << "adding new include for file " << FileName.str() << " at line " << lineNum;
    includes.push_back(std::make_pair(lineNum, FileName.str()));
}

}

void
IncludeFinderAction::ExecuteAction()
{
    printf("ExecuteAction\n");
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
    typedef Includes::iterator It;
    printf("*** diagnosis report:\n");
    printf("%ld includes:\n", includes.size());

    for (It it = includes.begin(); it != includes.end(); ++it) {
        const unsigned int lineNum = it->first;
        const std::string &hdrPath = it->second;
        std::cout << lineNum << ": " << hdrPath << std::endl;
    }
}
