/*
 * originally taken from https://github.com/xaizek/self-inc-first
 * and adapted for latest clang 18.1.7 API and SEN integration.
 */
#pragma once

#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Lex/PPCallbacks.h>

using namespace clang;

struct IncludeInfo {
    unsigned int lineNum;
    std::string  fileName;
    std::string  filePath;
    bool         global;
};

class IncludeFinder : private PPCallbacks
{
public:
    IncludeFinder() { };

    void SetCompilerInstance(clang::CompilerInstance* compilerInstance) {
        this->compiler = compilerInstance;
    };

    std::vector<IncludeInfo*>     GetIncludes() { return includes; };

    std::unique_ptr<PPCallbacks>  createPreprocessorCallbacks();

    virtual void EndOfMainFile();

    virtual void InclusionDirective(SourceLocation HashLoc,
                                  const Token &IncludeTok,
                                  StringRef FileName,
                                  bool IsAngled,
                                  CharSourceRange FilenameRange,
                                  OptionalFileEntryRef File,
                                  StringRef SearchPath,
                                  StringRef RelativePath,
                                  const Module *SuggestedModule,
                                  bool ModuleImported,
                                  SrcMgr::CharacteristicKind FileType);

private:
    std::vector<IncludeInfo*>     includes;
    clang::CompilerInstance      *compiler;
};
