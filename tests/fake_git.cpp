// Offline process fixture. Never contacts GitHub or invokes a real Git client.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string url = argv[argc - 2];
#ifdef _WIN32
    int wideCount = 0;
    auto** wideArgs = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (!wideArgs || wideCount < 3) return 2;
    const std::filesystem::path root(wideArgs[wideCount - 1]);
    LocalFree(wideArgs);
#else
    const std::filesystem::path root(argv[argc - 1]);
#endif
    if (url.find("/oversize") != std::string::npos) {
        std::ofstream output(root / "large.bin", std::ios::binary);
        output << std::string(8192, 'x');
        output.close();
    }
    if (url.find("/timeout") != std::string::npos || url.find("/oversize") != std::string::npos) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    std::ofstream(root / "cloned.txt") << "offline clone fixture";
    return 0;
}
