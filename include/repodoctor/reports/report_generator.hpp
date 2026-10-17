#pragma once

#include "repodoctor/engine/scan_engine.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace repodoctor {

enum class ReportFormat : std::uint8_t {
    Console = 0,
    Json = 1,
    Sarif = 2,
    Html = 3
};

[[nodiscard]] std::optional<ReportFormat> parseReportFormat(std::string_view formatStr);
[[nodiscard]] std::string toString(ReportFormat format);

class ReportGenerator {
public:
    ReportGenerator() = default;

    [[nodiscard]] std::string generate(
        const ScanResult& result,
        ReportFormat format) const;

    [[nodiscard]] std::string generateConsoleReport(const ScanResult& result) const;
    [[nodiscard]] std::string generateJsonReport(const ScanResult& result) const;
    [[nodiscard]] std::string generateSarifReport(const ScanResult& result) const;
    [[nodiscard]] std::string generateHtmlReport(const ScanResult& result) const;

    bool writeToFile(
        const ScanResult& result,
        ReportFormat format,
        const std::filesystem::path& outputPath,
        std::string* errorOut = nullptr) const;
};

}  // namespace repodoctor
