#pragma once

#include "repodoctor/core/types.hpp"
#include <atomic>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <cwchar>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace repodoctor::detail {

inline std::uint64_t checkoutSize(const std::filesystem::path& root) {
    std::uint64_t total = 0;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec), end;
    while (it != end) {
        std::error_code entryEc;
        if (!it->is_symlink(entryEc) && it->is_regular_file(entryEc)) {
            const auto size = it->file_size(entryEc);
            if (!entryEc) total += size;
        }
        it.increment(ec);
        if (ec) break;
    }
    return total;
}

// No shell: URL and destination are separate process arguments on both platforms.
inline std::string cloneRepository(const std::string& url,
    const std::filesystem::path& destination, int timeoutSeconds,
    std::uint64_t maxBytes, const std::atomic<bool>* cancelled) {
    if (timeoutSeconds <= 0 || maxBytes == 0) return "Invalid clone limits.";
    if (cancelled && cancelled->load()) return "Scan cancelled by user.";
    const auto started = std::chrono::steady_clock::now();
#ifdef _WIN32
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return "Cannot create clone process job.";
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        return "Cannot configure clone process job.";
    }
    std::wstring command = L"git -c core.hooksPath=NUL -c protocol.file.allow=never "
        L"-c credential.interactive=false -c credential.helper= -c core.askPass= "
        L"-c filter.lfs.smudge= -c filter.lfs.required=false "
        L"clone --depth 1 --single-branch --no-recurse-submodules --quiet \"";
    command += utf8ToPath(url).native() + L"\" \"" + destination.native() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<std::wstring> entries;
    if (auto* inherited = GetEnvironmentStringsW()) {
        for (auto* entry = inherited; *entry; entry += std::wcslen(entry) + 1) {
            std::wstring value(entry);
            auto key = value.substr(0, value.find(L'='));
            std::transform(key.begin(), key.end(), key.begin(),
                [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
            if (key != L"GIT_TERMINAL_PROMPT" && key != L"GIT_ASKPASS") entries.push_back(value);
        }
        FreeEnvironmentStringsW(inherited);
    }
    entries.emplace_back(L"GIT_TERMINAL_PROMPT=0");
    entries.emplace_back(L"GIT_ASKPASS=");
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    std::wstring environment;
    for (const auto& entry : entries) { environment += entry; environment += L'\0'; }
    environment += L'\0';
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
            environment.data(), nullptr, &startup, &process)) {
        CloseHandle(job);
        return "Cannot launch git. Ensure git is installed.";
    }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        return "Cannot supervise clone process.";
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
#else
    std::vector<std::string> args = {"git", "-c", "core.hooksPath=/dev/null", "-c", "protocol.file.allow=never",
        "-c", "credential.helper=", "-c", "filter.lfs.smudge=", "-c", "filter.lfs.required=false",
        "clone", "--depth", "1", "--single-branch", "--no-recurse-submodules", "--quiet",
        url, destination.native()};
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    std::vector<std::string> environment;
    for (char** entry = environ; *entry; ++entry) {
        const std::string value(*entry);
        if (value.rfind("GIT_TERMINAL_PROMPT=", 0) != 0 && value.rfind("GIT_ASKPASS=", 0) != 0)
            environment.push_back(value);
    }
    environment.emplace_back("GIT_TERMINAL_PROMPT=0");
    environment.emplace_back("GIT_ASKPASS=/bin/false");
    std::vector<char*> envp;
    for (auto& entry : environment) envp.push_back(entry.data());
    envp.push_back(nullptr);
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_t actions;
    if (posix_spawnattr_init(&attributes) != 0) return "Cannot initialize clone process.";
    if (posix_spawn_file_actions_init(&actions) != 0) {
        posix_spawnattr_destroy(&attributes);
        return "Cannot initialize clone streams.";
    }
    int setupError = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    setupError |= posix_spawnattr_setpgroup(&attributes, 0);
    for (const int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO})
        setupError |= posix_spawn_file_actions_addopen(&actions, fd, "/dev/null", O_RDWR, 0);
    pid_t pid = -1;
    const int spawnError = setupError ? setupError :
        posix_spawnp(&pid, "git", &actions, &attributes, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (spawnError != 0) return "Cannot launch git process.";
#endif
    std::string error;
    bool succeeded = false;
    for (;;) {
        if (cancelled && cancelled->load()) error = "Scan cancelled by user.";
        else if (std::chrono::steady_clock::now() - started >= std::chrono::seconds(timeoutSeconds))
            error = "GitHub clone timed out.";
        else if (checkoutSize(destination) > maxBytes) error = "GitHub clone exceeded size limit.";
        if (!error.empty()) break;
#ifdef _WIN32
        const DWORD waited = WaitForSingleObject(process.hProcess, 100);
        if (waited == WAIT_OBJECT_0) {
            DWORD code = 1;
            GetExitCodeProcess(process.hProcess, &code);
            succeeded = code == 0;
            break;
        }
        if (waited == WAIT_FAILED) { error = "Cannot monitor clone process."; break; }
#else
        int status = 0;
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) { succeeded = WIFEXITED(status) && WEXITSTATUS(status) == 0; break; }
        if (waited < 0) { error = "Cannot monitor clone process."; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
    }
#ifdef _WIN32
    // Closing the job terminates any remaining git helper processes.
    CloseHandle(job);
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hProcess);
#else
    kill(-pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
#endif
    if (!error.empty()) return error;
    if (!succeeded) return "Git clone failed. Check the public URL and network connection.";
    if (checkoutSize(destination) > maxBytes) return "GitHub clone exceeded size limit.";
    return {};
}
} // namespace repodoctor::detail
