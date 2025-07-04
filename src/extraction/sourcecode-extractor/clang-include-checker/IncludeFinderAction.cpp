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
    includeFinder->SetCompilerInstance(&getCompilerInstance());
    getCompilerInstance().getPreprocessor().addPPCallbacks(
        includeFinder->createPreprocessorCallbacks()
    );

    // only parse a single file and don't follow dependency chain
    getCompilerInstance().getPreprocessor().getPreprocessorOpts().SingleFileParseMode = true;

    std::cout << "calling executeAction\n";
    PreprocessOnlyAction::ExecuteAction();
}

void IncludeFinderAction::EndSourceFileAction()
{
    std::cout << "end of file reached." << std::endl;
}
