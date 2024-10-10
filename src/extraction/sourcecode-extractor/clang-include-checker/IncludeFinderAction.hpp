/*
 * originally taken from https://github.com/xaizek/self-inc-first
 * and adapted for latest clang 18.1.7 API and SEN integration.
 */
#pragma once

#include "IncludeFinder.hpp"
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>

using namespace clang;
using namespace clang::tooling;

class IncludeFinderAction : public clang::PreprocessOnlyAction {
public:
  IncludeFinderAction(IncludeFinder *includeFinder);

protected:
  virtual void ExecuteAction();
  virtual void EndSourceFileAction();

private:
  IncludeFinder *includeFinder;
};

inline std::unique_ptr<FrontendActionFactory>
customFrontendActionFactory(IncludeFinder *finder) {
  class SimpleFrontendActionFactory : public FrontendActionFactory {
  public:
    SimpleFrontendActionFactory(IncludeFinder *finder) : mFinder(finder) {}

    std::unique_ptr<FrontendAction> create() override {
      return std::make_unique<IncludeFinderAction>(mFinder);
    }

  private:
    IncludeFinder *mFinder;
  };

  return std::unique_ptr<FrontendActionFactory>(
      new SimpleFrontendActionFactory(finder));
}