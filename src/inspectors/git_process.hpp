#pragma once

#include "repodoctor/core/types.hpp"
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace repodoctor::detail {

// Git arguments never pass through a shell. Bound output, execution time and child lifetime.
inline std::string readGit(const std::filesystem::path& root,
    const std::vector<std::string>& arguments, std::size_t maxBytes = 32 * 1024 * 1024,
    const std::atomic<bool>* cancel = nullptr) {
    if (cancel && cancel->load()) throw std::runtime_error("Scan cancelled by user.");
    std::vector<std::string> args = {"git", "--no-pager", "-c", "core.hooksPath=",
        "-c", "core.fsmonitor=false", "-c", "diff.external=", "-c", "core.quotePath=false",
        "-c", "protocol.allow=never", "-c", "protocol.file.allow=never",
        "-c", "credential.helper=", "-c", "credential.interactive=false", "-c", "core.askPass=",
        "-C", pathToUtf8(root)};
    args.insert(args.end(), arguments.begin(), arguments.end());
#ifdef _WIN32
    auto quote = [](const std::wstring& value) {
        std::wstring out = L"\""; std::size_t slashes = 0;
        for (const auto c : value) {
            if (c == L'\\') { ++slashes; continue; }
            if (c == L'\"') { out.append(slashes * 2 + 1, L'\\'); out += c; }
            else { out.append(slashes, L'\\'); out += c; }
            slashes = 0;
        }
        out.append(slashes * 2, L'\\'); out += L'\"'; return out;
    };
    std::wstring command;
    for (const auto& arg : args) { if (!command.empty()) command += L' '; command += quote(utf8ToPath(arg).native()); }
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE reader = nullptr, writer = nullptr;
    if (!CreatePipe(&reader, &writer, &sa, 0)) throw std::runtime_error("Cannot create Git pipe.");
    SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
    HANDLE nullHandle = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(reader); CloseHandle(writer); CloseHandle(nullHandle); if (job) CloseHandle(job);
        throw std::runtime_error("Cannot supervise Git process.");
    }
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writer; startup.hStdError = nullHandle; startup.hStdInput = nullHandle;
    PROCESS_INFORMATION process{};
    const bool launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process) != 0;
    CloseHandle(writer); CloseHandle(nullHandle);
    if (!launched) { CloseHandle(reader); CloseHandle(job); throw std::runtime_error("Cannot launch Git; install Git to use incremental/history scans."); }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1); CloseHandle(process.hThread); CloseHandle(process.hProcess);
        CloseHandle(reader); CloseHandle(job); throw std::runtime_error("Cannot supervise Git process.");
    }
    ResumeThread(process.hThread); CloseHandle(process.hThread);
#else
    int pipes[2];
    if (pipe(pipes) != 0) throw std::runtime_error("Cannot create Git pipe.");
    fcntl(pipes[0], F_SETFD, FD_CLOEXEC); fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
    std::vector<char*> argv; for (auto& a : args) argv.push_back(a.data()); argv.push_back(nullptr);
    posix_spawn_file_actions_t actions; posix_spawnattr_t attr;
    int setup = posix_spawn_file_actions_init(&actions);
    if (setup) { close(pipes[0]); close(pipes[1]); throw std::runtime_error("Cannot initialize Git process."); }
    setup = posix_spawnattr_init(&attr);
    if (setup) { posix_spawn_file_actions_destroy(&actions); close(pipes[0]); close(pipes[1]); throw std::runtime_error("Cannot initialize Git process."); }
    setup |= posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    setup |= posix_spawn_file_actions_addclose(&actions, pipes[0]);
    setup |= posix_spawn_file_actions_addclose(&actions, pipes[1]);
    setup |= posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    setup |= posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    setup |= posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    setup |= posix_spawnattr_setpgroup(&attr, 0);
    pid_t pid = -1;
    const int rc = setup ? setup : posix_spawnp(&pid, "git", &actions, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attr); close(pipes[1]);
    if (rc != 0) { close(pipes[0]); throw std::runtime_error("Cannot launch Git; install Git to use incremental/history scans."); }
    fcntl(pipes[0], F_SETFL, O_NONBLOCK);
#endif
    const auto start = std::chrono::steady_clock::now();
    std::string output, error; bool done = false; int exitCode = 1;
    for (;;) {
        if (cancel && cancel->load()) { error = "Scan cancelled by user."; break; }
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(30)) { error = "Git inspection timed out."; break; }
        const bool wasDone = done;
        char buffer[8192]; std::size_t count = 0;
#ifdef _WIN32
        DWORD available = 0, received = 0;
        if (PeekNamedPipe(reader, nullptr, 0, nullptr, &available, nullptr) && available) {
            if (ReadFile(reader, buffer, static_cast<DWORD>(sizeof(buffer)), &received, nullptr)) count = received;
        }
        if (!done && WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
            DWORD code = 1; GetExitCodeProcess(process.hProcess, &code); exitCode = static_cast<int>(code); done = true;
        }
#else
        const auto received = read(pipes[0], buffer, sizeof(buffer));
        if (received > 0) count = static_cast<std::size_t>(received);
        if (!done) {
            int status = 0; const auto waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) { done = true; exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1; }
            else if (waited < 0 && errno != EINTR) { error = "Cannot monitor Git process."; break; }
        }
#endif
        if (count > maxBytes - output.size()) { error = "Git inspection output limit exceeded."; break; }
        output.append(buffer, count);
        if (done && wasDone && count == 0) break;
        if (!count) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
#ifdef _WIN32
    CloseHandle(job); WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hProcess); CloseHandle(reader);
#else
    kill(-pid, SIGKILL);
    if (!done) { int status = 0; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
    close(pipes[0]);
#endif
    if (!error.empty()) throw std::runtime_error(error);
    if (exitCode != 0) throw std::runtime_error("Git inspection failed; check repository, index and revision.");
    return output;
}
} // namespace repodoctor::detail
