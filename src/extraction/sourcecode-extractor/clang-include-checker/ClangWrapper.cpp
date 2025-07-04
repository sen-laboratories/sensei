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
}

int ClangWrapper::run(BMessage *reply) {
    const char* argv[3];
    argv[0] = "clang++";
    argv[1] = fSourcePath;
    argv[2] = "--";		// this is important, else clang-tools won't run!
    int argc = 3;

    llvm::Expected<CommonOptionsParser> optionsParserOpt = CommonOptionsParser::create(argc, argv, toolCategory);
    if (!optionsParserOpt) {
        llvm::errs() << optionsParserOpt.takeError();
        std::cerr << "failed to setup parser: " << llvm::errs().error() << std::endl;
        return -1;
    }
    CommonOptionsParser& optionsParser = optionsParserOpt.get();

    clang::tooling::ClangTool tool(
        optionsParser.getCompilations(),
        optionsParser.getSourcePathList());

    IncludeFinder *includeFinder = new IncludeFinder();
    int result = tool.run(customFrontendActionFactory(includeFinder).get());

    if (result != 0) {
        printf("there were errors scanning path '%s' for includes.\n", fSourcePath);
        // still continue with the includes we've got, might just be some missing ones.
        // we iterate over those below and return the error result anyway.
    }

    // prepare result
    auto includes = includeFinder->GetIncludes();
    std::vector<IncludeInfo*>::iterator it;
    BMessage item;
    int32 msgIndex = 0;

    printf("got %zu includes for path %s:\n", includes.size(), fSourcePath);

    for (it = includes.begin(); it != includes.end(); ++it, msgIndex++) {
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

    return result;
}
