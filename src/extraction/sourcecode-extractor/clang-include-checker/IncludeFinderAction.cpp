#include <iostream>
#include <clang/Frontend/CompilerInstance.h>
#include "IncludeFinder.hpp"
#include "IncludeFinderAction.hpp"

IncludeFinderAction::IncludeFinderAction(IncludeFinder* includeFinder)
: includeFinder(includeFinder)
{
}

void
IncludeFinderAction::ExecuteAction()
{
    std::cout << "got IncludeFinder instance " << includeFinder << std::endl;
    getCompilerInstance().getPreprocessor().addPPCallbacks(
        includeFinder->createPreprocessorCallbacks()
    );

    // only parse a single file and don't follow dependency chain
    getCompilerInstance().getPreprocessor().getPreprocessorOpts().SingleFileParseMode = true;

    PreprocessOnlyAction::ExecuteAction();
}

void IncludeFinderAction::EndSourceFileAction()
{
    std::cout << "end of file reached." << std::endl;
    IncludeFinder::getInstance()->EndOfMainFile();
}
