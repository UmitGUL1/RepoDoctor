#pragma once
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace repodoctor::detail {
// Exclusive creation refuses existing files and symlinks, even if another process races us.
inline bool writeNewFile(const std::filesystem::path& path, std::string_view content) {
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool success = true;
    while (!content.empty()) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(content.size(), 1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(file, content.data(), chunk, &written, nullptr) || written == 0) {
            success = false;
            break;
        }
        content.remove_prefix(written);
    }
    if (success) success = FlushFileBuffers(file) != 0;
    CloseHandle(file);
#else
    const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) return false;
    bool success = true;
    while (!content.empty()) {
        const auto written = write(file, content.data(), std::min<std::size_t>(content.size(), 1024 * 1024));
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { success = false; break; }
        content.remove_prefix(static_cast<std::size_t>(written));
    }
    if (success) success = fsync(file) == 0;
    if (close(file) != 0) success = false;
#endif
    if (!success) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    return success;
}
} // namespace repodoctor::detail
