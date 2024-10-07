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

ClangWrapper::~ClangWrapper() {}

int ClangWrapper::run() {
    const char* argv[3];
    argv[0] = "clang";
    argv[1] = fSourcePath;
    argv[2] = "--";
    int argc = 3;

    auto ExpectedParser = CommonOptionsParser::create(argc, argv, toolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return -1;
    }
    CommonOptionsParser& optionsParser = ExpectedParser.get();

    clang::tooling::ClangTool* tool = new ClangTool(
        optionsParser.getCompilations(),
        optionsParser.getSourcePathList());

    return tool->run(newFrontendActionFactory<IncludeFinderAction>().get());
}
