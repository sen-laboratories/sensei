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
#include <iostream>

#include <FindDirectory.h>
#include <Path.h>
#include <Message.h>

#include <clang/Basic/Diagnostic.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>

#include "ClangWrapper.hpp"
#include "IncludeFinderAction.hpp"
#include <sen/Sen.h>
#include <sen/Sensei.h>

using namespace clang::tooling;
static llvm::cl::OptionCategory toolCategory("Include scanner");
static llvm::cl::extrahelp commonHelp(CommonOptionsParser::HelpMessage);

ClangWrapper::ClangWrapper(const char* filePath, bool self) {
   fSourcePath = filePath;
   fSelf       = self;
}

ClangWrapper::~ClangWrapper() {
}

int ClangWrapper::run(BMessage *reply) {
std::vector<const char*> args;
    args.push_back("clang-22");
    args.push_back(fSourcePath);

    // Clang tooling requires "--" to separate tool arguments from compiler arguments
    // when bypassing a compile_commands.json database.
    args.push_back("--");

    BPath pathSys, pathSysExtra, pathUser, pathUserExtra;
    status_t status;

    status = find_directory(B_SYSTEM_HEADERS_DIRECTORY, &pathSys);
    if (status == B_OK) {
        args.push_back("-I");
        args.push_back(pathSys.Path());
    }

    status = find_directory(B_SYSTEM_NONPACKAGED_HEADERS_DIRECTORY, &pathSysExtra);
    if (status == B_OK) {
        args.push_back("-I");
        args.push_back(pathSysExtra.Path());
    }

    status = find_directory(B_USER_HEADERS_DIRECTORY, &pathUser);
    if (status == B_OK) {
        args.push_back("-I");
        args.push_back(pathUser.Path());
    }

    status = find_directory(B_USER_NONPACKAGED_HEADERS_DIRECTORY, &pathUserExtra);
    if (status == B_OK) {
        args.push_back("-I");
        args.push_back(pathUserExtra.Path());
    }

    int argc = static_cast<int>(args.size());

    llvm::Expected<CommonOptionsParser> optionsParserOpt = CommonOptionsParser::create(argc, args.data(), toolCategory);    if (!optionsParserOpt) {
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
        printf("warning: there were errors scanning path '%s' for includes.\n", fSourcePath);
        // still continue with the includes we've got, might just be some missing ones.
        // since we cannot expect a CMakeLists.txt to exist, this is acceptable.
        result = B_OK;
    }

    // prepare result
    auto includes = includeFinder->GetIncludes();
    std::vector<IncludeInfo*>::iterator it;

    printf("got %zu includes for path %s:\n", includes.size(), fSourcePath);

    // build flat item fields array for compact result
    BMessage item;

    for (it = includes.begin(); it != includes.end(); ++it) {
        unsigned int lineNum    = (*it)->lineNum;
        std::string  fileName   = (*it)->fileName;
        std::string  searchPath = (*it)->filePath;
        bool         isGlobal   = (*it)->global;

        // same for inward (self) and outward relation
        item.AddString(sensei::key::kLabel, fileName.c_str());

        // path is mapped to sen::attr::kToPath so SEN can resolve the target transparently
        BPath path(searchPath.c_str(), fileName.c_str());
        item.AddString("path", path.Path());

        item.AddBool("global", isGlobal);

        // self relations have additional inward pointing properties
        if (fSelf) {
            item.AddInt32("line", lineNum);
        }
    }

    reply->AddMessage(sensei::key::kItem, &item);

    return result;
}
