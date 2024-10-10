/*
 * originally taken from https://github.com/xaizek/self-inc-first
 * and adapted for latest clang 18.1.7 API and SEN integration.
 */
#pragma once

#include <mutex>
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
    static IncludeFinder* getInstance() {
        if (instance == NULL) {
            std::lock_guard<std::mutex> lock(mutex);
            if (instance == NULL) {
                instance = new IncludeFinder();
                printf("IncludeFinder: created new instance.\n");
            } else {
                printf("IncludeFinder: return existing instance.\n");
            }
            std::lock_guard<std::mutex> unlock(mutex);
        }
        return instance;
    };

    std::unique_ptr<PPCallbacks> createPreprocessorCallbacks();

    virtual void EndOfMainFile();

    virtual void InclusionDirective(SourceLocation HashLoc,
                                  const Token &IncludeTok,
                                  StringRef FileName,
                                  bool IsAngled,
                                  CharSourceRange FilenameRange,
                                  OptionalFileEntryRef File,
                                  StringRef SearchPath,
                                  StringRef RelativePath,
                                  const Module *Imported,
                                  SrcMgr::CharacteristicKind FileType);

private:
    IncludeFinder() {};
    IncludeFinder(const IncludeFinder& other) = delete;

    std::vector<IncludeInfo*>     includes;
    static IncludeFinder*         instance;
    static std::mutex             mutex;
};
