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

#include <Path.h>
#include <iostream>
#include <Message.h>

#include <clang/Basic/Diagnostic.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>

#include "ClangWrapper.hpp"
#include "IncludeFinderAction.hpp"

using namespace clang::tooling;
static llvm::cl::OptionCategory toolCategory("Include scanner");
static llvm::cl::extrahelp commonHelp(CommonOptionsParser::HelpMessage);

ClangWrapper::ClangWrapper(const char* filePath) {
   fSourcePath = filePath;
}

ClangWrapper::~ClangWrapper() {
    delete fSourcePath;
    printf("ClangWrapper d'tor.\n");
}

int ClangWrapper::run(BMessage *reply) {
    const char* argv[3];
    argv[0] = "clang++";
    argv[1] = fSourcePath;
    argv[2] = "--";
    int argc = 3;

    llvm::Expected<CommonOptionsParser> optionsParserOpt = CommonOptionsParser::create(argc, argv, toolCategory);
    if (!optionsParserOpt) {
        llvm::errs() << optionsParserOpt.takeError();
        return -1;
    }
    CommonOptionsParser& optionsParser = optionsParserOpt.get();

    clang::tooling::ClangTool tool(
        optionsParser.getCompilations(),
        optionsParser.getSourcePathList());

    IncludeFinder includeFinder;
    int result = tool.run(customFrontendActionFactory(&includeFinder).get());

    // prepare result
    std::vector<IncludeInfo*>* includes = includeFinder.GetIncludes();
    std::vector<IncludeInfo*>::iterator it;
    BMessage item;
    int32 msgIndex = 0;

    for (it = includes->begin(); it != includes->end(); ++it, msgIndex++) {
        unsigned int lineNum =    (*it)->lineNum;
        std::string  hdrPath =    (*it)->fileName;
        std::string  searchPath = (*it)->filePath;
        bool         isGlobal =   (*it)->global;

        std::cout << lineNum << ": " << hdrPath << " from " << searchPath <<
            (isGlobal ? " (global)" : "(local)") << std::endl;

        BPath path(hdrPath.c_str());

        item.AddString("label", path.Leaf());
        item.AddString("path", path.Path());
        item.AddString("spath", searchPath.c_str());
        item.AddInt32("line", lineNum);
        item.AddBool("global", isGlobal);
    }
    reply->AddMessage("item", new BMessage(item));

    printf("in ClangWrapper, end of run.\n");
    return result;
}
