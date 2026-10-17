#include "repodoctor/engine/scan_engine.hpp"
#include "repodoctor/sources/irepository_source.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

namespace {

class SyntheticRepoFixture {
public:
    explicit SyntheticRepoFixture(std::size_t fileCount) {
        const auto nowTicks =
            std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("repodoctor_bench_" + std::to_string(fileCount) + "_" +
                 std::to_string(nowTicks & 0xFFFFFF));
        std::filesystem::create_directories(root_ / ".git" / "objects");
        std::filesystem::create_directories(root_ / ".git" / "refs" / "heads");

        writeFile(".git/HEAD", "ref: refs/heads/main\n");
        writeFile(".git/refs/heads/main", "0123456789abcdef0123456789abcdef01234567\n");
        writeFile(".gitignore", "build/\n*.o\n.env\n");
        writeFile(
            "README.md",
            "# Benchmark Project\n\n## Installation\n\nRun cmake.\n\n## Usage\n\nRun app.\n");
        writeFile("CONTRIBUTING.md", "# Contributing\nRun tests.\n");
        writeFile("LICENSE", "MIT License\nCopyright (c) 2026 RepoDoctor\n");
        writeFile("CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(Bench)\n");
        writeFile(
            ".github/workflows/ci.yml",
            "name: CI\non: [push]\npermissions:\n  contents: read\njobs:\n  b:\n    runs-on: ubuntu-latest\n    steps:\n      - uses: actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11\n");

        const std::size_t remaining = (fileCount > 6) ? (fileCount - 6) : 1;
        for (std::size_t i = 0; i < remaining; ++i) {
            const std::string dir = "src/module_" + std::to_string(i / 50);
            const std::string rel =
                (i % 10 == 0)
                    ? ("tests/test_" + std::to_string(i) + ".cpp")
                    : (dir + "/file_" + std::to_string(i) + ".cpp");
            writeFile(
                rel,
                "#include <cstdint>\n"
                "namespace bench {\n"
                "int compute_" + std::to_string(i) + "(int x) { return x * 2 + " +
                    std::to_string(i) + "; }\n"
                "} // namespace bench\n");
        }
    }

    ~SyntheticRepoFixture() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    const std::filesystem::path& path() const noexcept { return root_; }

private:
    void writeFile(const std::string& relPath, const std::string& content) const {
        const auto full = root_ / relPath;
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);
        std::ofstream out(full, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::filesystem::path root_;
};

void runBenchmarkTier(const std::string& tierName, std::size_t fileCount) {
    SyntheticRepoFixture fixture(fileCount);
    repodoctor::LocalRepositorySource source(fixture.path());
    repodoctor::ScanEngine engine;

    const auto start = std::chrono::steady_clock::now();
    const repodoctor::ScanResult res = engine.scan(source);
    const auto end = std::chrono::steady_clock::now();

    const double elapsedMs =
        std::chrono::duration<double, std::milli>(end - start).count();
    const double filesPerSec =
        (elapsedMs > 0.0)
            ? (static_cast<double>(res.statistics.totalFilesDiscovered) / (elapsedMs / 1000.0))
            : 0.0;

    std::cout << std::left << std::setw(22) << tierName
              << std::right << std::setw(10) << res.statistics.totalFilesDiscovered
              << std::setw(14) << (res.statistics.totalBytesRead / 1024ULL) << " KB"
              << std::setw(14) << std::fixed << std::setprecision(2) << elapsedMs << " ms"
              << std::setw(14) << std::fixed << std::setprecision(0) << filesPerSec << " files/s"
              << "   Score: " << std::setprecision(1) << res.healthScore.overallScore << "/100\n";
}

}  // namespace

int main() {
    std::cout << "========================================================================================\n";
    std::cout << " RepoDoctor Synthetic Repository Performance Benchmarks\n";
    std::cout << "========================================================================================\n";
    std::cout << std::left << std::setw(22) << "TIER"
              << std::right << std::setw(10) << "FILES"
              << std::setw(17) << "BYTES READ"
              << std::setw(17) << "SCAN DURATION"
              << std::setw(22) << "THROUGHPUT\n";
    std::cout << "----------------------------------------------------------------------------------------\n";

    runBenchmarkTier("Small (50 files)", 50);
    runBenchmarkTier("Medium (500 files)", 500);
    runBenchmarkTier("Large (2000 files)", 2000);

    std::cout << "========================================================================================\n";
    return 0;
}
