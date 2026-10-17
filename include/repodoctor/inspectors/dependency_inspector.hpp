#pragma once

#include "repodoctor/core/types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace repodoctor {

enum class DependencyEcosystem : std::uint8_t {
    CMakeVcpkg = 0,
    Npm = 1,
    Python = 2
};

[[nodiscard]] std::string toString(DependencyEcosystem ecosystem);

struct DependencyDeclaration {
    DependencyEcosystem ecosystem{DependencyEcosystem::CMakeVcpkg};
    std::string name;
    std::string versionConstraint;
    std::string manifestPath;
    std::size_t lineNumber{0};
    bool isDevDependency{false};
    bool isWildcardOrUnpinned{false};
};

struct DependencyManifest {
    DependencyEcosystem ecosystem{DependencyEcosystem::CMakeVcpkg};
    std::string manifestRelativePath;
    bool hasLockfile{false};
    std::string lockfileRelativePath;
    bool isDeprecatedFormat{false};
    std::string formatWarning;
    std::vector<DependencyDeclaration> dependencies;
};

struct AdvisoryMatch {
    std::string packageName;
    DependencyEcosystem ecosystem{DependencyEcosystem::Npm};
    std::string installedOrDeclaredConstraint;
    std::string advisoryId;
    std::string advisorySource;
    Severity severity{Severity::High};
    std::string summary;
    std::string fixedVersion;
};

struct DependencyInspectionReport {
    std::vector<DependencyManifest> manifests;
    std::vector<std::string> inconsistencyNotes;
    std::vector<AdvisoryMatch> verifiedAdvisories;
    bool advisoryDatabaseChecked{false};
    std::string advisorySourceAttribution;
};

class DependencyInspector {
public:
    DependencyInspector() = default;

    [[nodiscard]] DependencyInspectionReport inspect(
        const std::filesystem::path& repoRoot,
        const std::vector<std::string>& allRelativeFiles,
        bool checkOptionalAdvisoryCache = false) const;
};

}  // namespace repodoctor
