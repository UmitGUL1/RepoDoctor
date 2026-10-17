#pragma once
#include "repodoctor/core/types.hpp"
#include <cstdlib>
#include <string>

class FakeGitEnvironment {
public:
    FakeGitEnvironment() {
#ifdef _WIN32
        const auto* previous = _wgetenv(L"PATH");
        hadPath_ = previous != nullptr;
        old_ = previous ? previous : L"";
        const auto next = repodoctor::utf8ToPath(REPODOCTOR_FAKE_GIT_DIR).native() + L";" + old_;
        _wputenv_s(L"PATH", next.c_str());
#else
        const auto* previous = std::getenv("PATH");
        hadPath_ = previous != nullptr;
        old_ = previous ? previous : "";
        const auto next = std::string(REPODOCTOR_FAKE_GIT_DIR) + ":" + old_;
        setenv("PATH", next.c_str(), 1);
#endif
    }
    ~FakeGitEnvironment() {
#ifdef _WIN32
        _wputenv_s(L"PATH", old_.c_str());
#else
        if (hadPath_) setenv("PATH", old_.c_str(), 1);
        else unsetenv("PATH");
#endif
    }
    FakeGitEnvironment(const FakeGitEnvironment&) = delete;
    FakeGitEnvironment& operator=(const FakeGitEnvironment&) = delete;
private:
    bool hadPath_{false};
#ifdef _WIN32
    std::wstring old_;
#else
    std::string old_;
#endif
};
