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
#include <clang/Lex/PPCallbacks.h>

#include <iostream>
#include <string>
#include <vector>
#include <utility>

namespace
{

class CallbacksProxy : public clang::PPCallbacks
{
public:
    inline CallbacksProxy(clang::PPCallbacks &master);

public:
   virtual inline void InclusionDirective(clang::SourceLocation hashLoc,
                                           const clang::Token&  includeToken,
                                           clang::StringRef     fileName,
                                           bool                 isAngled,
                                           clang::CharSourceRange filenameRange,
                                           clang::OptionalFileEntryRef file,
                                           clang::StringRef     searchPath,
                                           clang::StringRef     relativePath,
                                           const clang::Module *suggestedModule,
                                           bool                 imported,
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
                                           const clang::Token&  includeToken,
                                           clang::StringRef     fileName,
                                           bool                 isAngled,
                                           clang::CharSourceRange filenameRange,
                                           clang::OptionalFileEntryRef file,
                                           clang::StringRef     searchPath,
                                           clang::StringRef     relativePath,
                                           const clang::Module *suggestedModule,
                                           bool                 imported,
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

class IncludeFinder : private clang::PPCallbacks
{
public:
    explicit inline IncludeFinder(const clang::CompilerInstance &compiler);

public:
    inline std::unique_ptr<clang::PPCallbacks> createPreprocessorCallbacks();

    inline void diagnoseAndReport();

    virtual inline void InclusionDirective(clang::SourceLocation hashLoc,
                                           const clang::Token &includeTok,
                                           clang::StringRef fileName,
                                           bool isAngled,
                                           clang::CharSourceRange filenameRange,
                                           const clang::FileEntry *file,
                                           clang::StringRef searchPath,
                                           clang::StringRef relativePath,
                                           const clang::Module *imported);

private:
    const clang::CompilerInstance &compiler;
    std::string name;

    typedef std::pair<int, std::string> IncludeInfo;
    typedef std::vector<IncludeInfo> Includes;
    Includes includes;
};

inline
IncludeFinder::IncludeFinder(const clang::CompilerInstance &compiler)
    : compiler(compiler)
{
    const clang::FileID mainFile = compiler.getSourceManager().getMainFileID();
    name = compiler.getSourceManager().getFileEntryRefForID(mainFile)->getName();
}

inline std::unique_ptr<clang::PPCallbacks>
IncludeFinder::createPreprocessorCallbacks()
{
    return std::unique_ptr<clang::PPCallbacks> (new CallbacksProxy(*this));
}

typedef std::vector<std::string> KnownHdrExts;
static KnownHdrExts
getKnownHdrExts()
{
    KnownHdrExts knownHdrExts;
    knownHdrExts.push_back("h");
    knownHdrExts.push_back("H");
    knownHdrExts.push_back("hpp");
    knownHdrExts.push_back("HPP");
    knownHdrExts.push_back("hxx");
    knownHdrExts.push_back("HXX");
    return knownHdrExts;
}
static const KnownHdrExts KNOWN_HDR_EXTS = getKnownHdrExts();

inline void
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

inline void
IncludeFinder::InclusionDirective(clang::SourceLocation hashLoc,
                                  const clang::Token &includeTok,
                                  clang::StringRef fileName,
                                  bool isAngled,
                                  clang::CharSourceRange filenameRange,
                                  const clang::FileEntry *file,
                                  clang::StringRef searchPath,
                                  clang::StringRef relativePath,
                                  const clang::Module *imported)
{
    printf("adding new inclusionDirective.\n");
    clang::SourceManager &sm = compiler.getSourceManager();
    const unsigned int lineNum = sm.getSpellingLineNumber(hashLoc);
    includes.push_back(std::make_pair(lineNum, fileName.data()));
}

}

void
IncludeFinderAction::ExecuteAction()
{
    IncludeFinder includeFinder(getCompilerInstance());
    getCompilerInstance().getPreprocessor().addPPCallbacks(
        includeFinder.createPreprocessorCallbacks()
    );

    clang::PreprocessOnlyAction::ExecuteAction();

    includeFinder.diagnoseAndReport();
}
