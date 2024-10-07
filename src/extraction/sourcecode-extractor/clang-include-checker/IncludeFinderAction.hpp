/*
 * originally taken from https://github.com/xaizek/self-inc-first
 * and adapted for latest clang 18.1.7 API and SEN integration.
 */
#pragma once

#include <clang/Frontend/FrontendActions.h>

class IncludeFinderAction : public clang::PreprocessOnlyAction
{
protected:
    virtual void ExecuteAction();
};
