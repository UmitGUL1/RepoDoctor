#include "repodoctor/inspectors/dependency_inspector.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace repodoctor {

namespace {

std::string trim(std::string_view sv) {
    std::size_t first = 0;
    while (first < sv.size() && std::isspace(static_cast<unsigned char>(sv[first]))) {
        ++first;
    }
    std::size_t last = sv.size();
    while (last > first && std::isspace(static_cast<unsigned char>(sv[last - 1]))) {
        --last;
    }
    return std::string(sv.substr(first, last - first));
}

bool isUnpinnedNpmVersion(std::string_view ver) {
    const std::string t = trim(ver);
    return t.empty() || t == "*" || t == "latest" || t == "x" || t == "X" ||
           t.rfind("http://", 0) == 0 || t.rfind("https://", 0) == 0 ||
           t.rfind("git+", 0) == 0 || t.rfind("github:", 0) == 0;
}

bool isUnpinnedPythonConstraint(std::string_view constraint) {
    const std::string t = trim(constraint);
    return t.empty() || t == "*";
}

void inspectNpmManifest(
    const std::filesystem::path& repoRoot,
    const std::string& relPath,
    const std::set<std::string>& fileSet,
    DependencyInspectionReport& report) {
    std::ifstream in(repoRoot / relPath);
    if (!in.is_open()) {
        return;
    }

    nlohmann::json pkg;
    try {
        in >> pkg;
    } catch (...) {
        DependencyManifest broken{};
        broken.ecosystem = DependencyEcosystem::Npm;
        broken.manifestRelativePath = relPath;
        broken.isDeprecatedFormat = true;
        broken.formatWarning = "Malformed JSON in " + relPath;
        report.manifests.push_back(std::move(broken));
        return;
    }

    if (!pkg.is_object()) {
        return;
    }

    DependencyManifest manifest{};
    manifest.ecosystem = DependencyEcosystem::Npm;
    manifest.manifestRelativePath = relPath;

    const auto parentDir = pathToGenericUtf8(utf8ToPath(relPath).parent_path());
    const auto prefix = parentDir.empty() ? std::string("") : (parentDir + "/");

    const std::vector<std::string> lockCandidates = {
        prefix + "package-lock.json",
        prefix + "yarn.lock",
        prefix + "pnpm-lock.yaml",
        prefix + "npm-shrinkwrap.json",
        "package-lock.json",
        "yarn.lock",
        "pnpm-lock.yaml"
    };
    for (const auto& candidate : lockCandidates) {
        if (fileSet.count(candidate) > 0) {
            manifest.hasLockfile = true;
            manifest.lockfileRelativePath = candidate;
            break;
        }
    }

    std::map<std::string, std::string> prodDeps;
    if (pkg.contains("dependencies") && pkg["dependencies"].is_object()) {
        for (auto it = pkg["dependencies"].begin(); it != pkg["dependencies"].end(); ++it) {
            if (!it.value().is_string()) {
                continue;
            }
            const std::string name = it.key();
            const std::string ver = it.value().get<std::string>();
            prodDeps[name] = ver;
            DependencyDeclaration decl{};
            decl.ecosystem = DependencyEcosystem::Npm;
            decl.name = name;
            decl.versionConstraint = ver;
            decl.manifestPath = relPath;
            decl.isDevDependency = false;
            decl.isWildcardOrUnpinned = isUnpinnedNpmVersion(ver);
            manifest.dependencies.push_back(std::move(decl));
        }
    }

    if (pkg.contains("devDependencies") && pkg["devDependencies"].is_object()) {
        for (auto it = pkg["devDependencies"].begin(); it != pkg["devDependencies"].end(); ++it) {
            if (!it.value().is_string()) {
                continue;
            }
            const std::string name = it.key();
            const std::string ver = it.value().get<std::string>();
            if (prodDeps.find(name) != prodDeps.end()) {
                report.inconsistencyNotes.push_back(
                    "Package '" + name + "' in '" + relPath +
                    "' is declared in both 'dependencies' (" + prodDeps[name] +
                    ") and 'devDependencies' (" + ver + ").");
            }
            DependencyDeclaration decl{};
            decl.ecosystem = DependencyEcosystem::Npm;
            decl.name = name;
            decl.versionConstraint = ver;
            decl.manifestPath = relPath;
            decl.isDevDependency = true;
            decl.isWildcardOrUnpinned = isUnpinnedNpmVersion(ver);
            manifest.dependencies.push_back(std::move(decl));
        }
    }

    report.manifests.push_back(std::move(manifest));
}

void inspectVcpkgManifest(
    const std::filesystem::path& repoRoot,
    const std::string& relPath,
    const std::set<std::string>& fileSet,
    DependencyInspectionReport& report) {
    std::ifstream in(repoRoot / relPath);
    if (!in.is_open()) {
        return;
    }

    nlohmann::json doc;
    try {
        in >> doc;
    } catch (...) {
        DependencyManifest broken{};
        broken.ecosystem = DependencyEcosystem::CMakeVcpkg;
        broken.manifestRelativePath = relPath;
        broken.isDeprecatedFormat = true;
        broken.formatWarning = "Malformed JSON in " + relPath;
        report.manifests.push_back(std::move(broken));
        return;
    }

    if (!doc.is_object()) {
        return;
    }

    DependencyManifest manifest{};
    manifest.ecosystem = DependencyEcosystem::CMakeVcpkg;
    manifest.manifestRelativePath = relPath;

    const bool hasBuiltinBaseline =
        doc.contains("builtin-baseline") && doc["builtin-baseline"].is_string() &&
        !doc["builtin-baseline"].get<std::string>().empty();
    const bool hasConfigJson = fileSet.count("vcpkg-configuration.json") > 0;
    manifest.hasLockfile = hasBuiltinBaseline || hasConfigJson;
    if (hasConfigJson) {
        manifest.lockfileRelativePath = "vcpkg-configuration.json";
    } else if (hasBuiltinBaseline) {
        manifest.lockfileRelativePath = relPath + "#builtin-baseline";
    }

    if (doc.contains("dependencies") && doc["dependencies"].is_array()) {
        for (const auto& dep : doc["dependencies"]) {
            if (dep.is_string()) {
                DependencyDeclaration decl{};
                decl.ecosystem = DependencyEcosystem::CMakeVcpkg;
                decl.name = dep.get<std::string>();
                decl.versionConstraint = hasBuiltinBaseline ? "baseline" : "";
                decl.manifestPath = relPath;
                decl.isWildcardOrUnpinned = !hasBuiltinBaseline && !hasConfigJson;
                manifest.dependencies.push_back(std::move(decl));
            } else if (dep.is_object() && dep.contains("name") && dep["name"].is_string()) {
                DependencyDeclaration decl{};
                decl.ecosystem = DependencyEcosystem::CMakeVcpkg;
                decl.name = dep["name"].get<std::string>();
                if (dep.contains("version>=") && dep["version>="].is_string()) {
                    decl.versionConstraint = ">=" + dep["version>="].get<std::string>();
                } else if (hasBuiltinBaseline) {
                    decl.versionConstraint = "baseline";
                }
                decl.manifestPath = relPath;
                decl.isWildcardOrUnpinned =
                    decl.versionConstraint.empty() && !hasBuiltinBaseline && !hasConfigJson;
                manifest.dependencies.push_back(std::move(decl));
            }
        }
    }

    report.manifests.push_back(std::move(manifest));
}

void inspectPythonRequirements(
    const std::filesystem::path& repoRoot,
    const std::string& relPath,
    const std::set<std::string>& fileSet,
    DependencyInspectionReport& report) {
    std::ifstream in(repoRoot / relPath);
    if (!in.is_open()) {
        return;
    }

    DependencyManifest manifest{};
    manifest.ecosystem = DependencyEcosystem::Python;
    manifest.manifestRelativePath = relPath;

    const std::vector<std::string> lockCandidates = {
        "poetry.lock", "uv.lock", "Pipfile.lock", "requirements.lock"};
    for (const auto& lockFile : lockCandidates) {
        if (fileSet.count(lockFile) > 0) {
            manifest.hasLockfile = true;
            manifest.lockfileRelativePath = lockFile;
            break;
        }
    }

    static const std::regex kReqRegex(
        R"(^([A-Za-z0-9_.-]+)(?:\[[A-Za-z0-9_,.-]+\])?\s*([<>=!~]+.*)?$)");

    std::string line;
    std::size_t lineNum = 0;
    std::set<std::string> seenPackages;
    bool allExactPinned = true;

    while (std::getline(in, line)) {
        ++lineNum;
        const auto hashPos = line.find('#');
        if (hashPos != std::string::npos) {
            line = line.substr(0, hashPos);
        }
        const std::string t = trim(line);
        if (t.empty() || t.front() == '-') {
            continue;
        }

        std::smatch m;
        if (std::regex_match(t, m, kReqRegex)) {
            std::string pkgName = m[1].str();
            std::string lowerName = pkgName;
            std::transform(
                lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });

            const std::string constraint = m[2].matched ? trim(m[2].str()) : "";
            if (seenPackages.count(lowerName) > 0) {
                report.inconsistencyNotes.push_back(
                    "Duplicate Python package '" + pkgName + "' declared in '" + relPath +
                    "' at line " + std::to_string(lineNum) + ".");
            }
            seenPackages.insert(lowerName);

            if (constraint.rfind("==", 0) != 0) {
                allExactPinned = false;
            }

            DependencyDeclaration decl{};
            decl.ecosystem = DependencyEcosystem::Python;
            decl.name = pkgName;
            decl.versionConstraint = constraint;
            decl.manifestPath = relPath;
            decl.lineNumber = lineNum;
            decl.isWildcardOrUnpinned = isUnpinnedPythonConstraint(constraint);
            manifest.dependencies.push_back(std::move(decl));
        }
    }

    if (!manifest.dependencies.empty() && allExactPinned) {
        manifest.hasLockfile = true;
        manifest.lockfileRelativePath = relPath;
    }

    report.manifests.push_back(std::move(manifest));
}

void inspectPyprojectToml(
    const std::filesystem::path& repoRoot,
    const std::string& relPath,
    const std::set<std::string>& fileSet,
    DependencyInspectionReport& report) {
    std::ifstream in(repoRoot / relPath);
    if (!in.is_open()) {
        return;
    }

    DependencyManifest manifest{};
    manifest.ecosystem = DependencyEcosystem::Python;
    manifest.manifestRelativePath = relPath;

    const std::vector<std::string> lockCandidates = {
        "poetry.lock", "uv.lock", "Pipfile.lock", "pdm.lock"};
    for (const auto& lockFile : lockCandidates) {
        if (fileSet.count(lockFile) > 0) {
            manifest.hasLockfile = true;
            manifest.lockfileRelativePath = lockFile;
            break;
        }
    }

    bool inDependenciesArray = false;
    bool inPoetryDepsSection = false;
    std::string line;
    std::size_t lineNum = 0;

    static const std::regex kQuotedReqRegex(
        R"###("([A-Za-z0-9_.-]+)(?:\[[A-Za-z0-9_,.-]+\])?\s*([^"]*)")###");
    static const std::regex kPoetryKeyValRegex(
        R"###(^([A-Za-z0-9_.-]+)\s*=\s*"([^"]*)")###");

    while (std::getline(in, line)) {
        ++lineNum;
        const std::string t = trim(line);
        if (t.empty() || t.front() == '#') {
            continue;
        }
        if (t.front() == '[' && t.back() == ']') {
            inDependenciesArray = false;
            inPoetryDepsSection = (t == "[tool.poetry.dependencies]");
            continue;
        }
        if (t.rfind("dependencies", 0) == 0 && t.find('[') != std::string::npos) {
            inDependenciesArray = true;
        }
        if (inDependenciesArray) {
            std::smatch m;
            if (std::regex_search(t, m, kQuotedReqRegex)) {
                DependencyDeclaration decl{};
                decl.ecosystem = DependencyEcosystem::Python;
                decl.name = m[1].str();
                decl.versionConstraint = trim(m[2].str());
                decl.manifestPath = relPath;
                decl.lineNumber = lineNum;
                decl.isWildcardOrUnpinned = isUnpinnedPythonConstraint(decl.versionConstraint);
                manifest.dependencies.push_back(std::move(decl));
            }
            if (t.find(']') != std::string::npos) {
                inDependenciesArray = false;
            }
        } else if (inPoetryDepsSection) {
            std::smatch m;
            if (std::regex_search(t, m, kPoetryKeyValRegex)) {
                const std::string name = m[1].str();
                if (name != "python") {
                    DependencyDeclaration decl{};
                    decl.ecosystem = DependencyEcosystem::Python;
                    decl.name = name;
                    decl.versionConstraint = trim(m[2].str());
                    decl.manifestPath = relPath;
                    decl.lineNumber = lineNum;
                    decl.isWildcardOrUnpinned = isUnpinnedPythonConstraint(decl.versionConstraint);
                    manifest.dependencies.push_back(std::move(decl));
                }
            }
        }
    }

    report.manifests.push_back(std::move(manifest));
}

void inspectOptionalAdvisoryCache(
    const std::filesystem::path& repoRoot,
    DependencyInspectionReport& report) {
    const auto cachePath = repoRoot / ".repodoctor-advisories.json";
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec) ||
        !std::filesystem::is_regular_file(cachePath, ec)) {
        return;
    }

    std::ifstream in(cachePath);
    if (!in.is_open()) {
        return;
    }

    nlohmann::json doc;
    try {
        in >> doc;
    } catch (...) {
        return;
    }

    if (!doc.is_object() || !doc.contains("advisories") || !doc["advisories"].is_array()) {
        return;
    }

    report.advisoryDatabaseChecked = true;
    report.advisorySourceAttribution =
        doc.value("source", "Local OSV Advisory Cache (.repodoctor-advisories.json)");

    for (const auto& adv : doc["advisories"]) {
        if (!adv.is_object()) {
            continue;
        }
        const std::string pkg = adv.value("package", "");
        const std::string affectedVersion = adv.value("affected_version", "");
        if (pkg.empty()) {
            continue;
        }

        for (const auto& manifest : report.manifests) {
            for (const auto& dep : manifest.dependencies) {
                if (dep.name == pkg &&
                    (affectedVersion.empty() ||
                     dep.versionConstraint.find(affectedVersion) != std::string::npos)) {
                    AdvisoryMatch match{};
                    match.packageName = dep.name;
                    match.ecosystem = dep.ecosystem;
                    match.installedOrDeclaredConstraint = dep.versionConstraint;
                    match.advisoryId = adv.value("id", "OSV-UNKNOWN");
                    match.advisorySource = report.advisorySourceAttribution;
                    match.severity =
                        parseSeverity(adv.value("severity", "HIGH")).value_or(Severity::High);
                    match.summary = adv.value("summary", "Advisory match in local OSV cache.");
                    match.fixedVersion = adv.value("fixed_version", "");
                    report.verifiedAdvisories.push_back(std::move(match));
                }
            }
        }
    }
}

}  // namespace

std::string toString(DependencyEcosystem ecosystem) {
    switch (ecosystem) {
        case DependencyEcosystem::CMakeVcpkg:
            return "CMake/vcpkg";
        case DependencyEcosystem::Npm:
            return "npm";
        case DependencyEcosystem::Python:
            return "Python";
    }
    return "Unknown";
}

DependencyInspectionReport DependencyInspector::inspect(
    const std::filesystem::path& repoRoot,
    const std::vector<std::string>& allRelativeFiles,
    bool checkOptionalAdvisoryCache) const {
    DependencyInspectionReport report{};
    const std::set<std::string> fileSet(allRelativeFiles.begin(), allRelativeFiles.end());

    for (const auto& relPath : allRelativeFiles) {
        const auto filename = pathToUtf8(utf8ToPath(relPath).filename());
        if (filename == "package.json") {
            inspectNpmManifest(repoRoot, relPath, fileSet, report);
        } else if (filename == "vcpkg.json") {
            inspectVcpkgManifest(repoRoot, relPath, fileSet, report);
        } else if (filename == "requirements.txt") {
            inspectPythonRequirements(repoRoot, relPath, fileSet, report);
        } else if (filename == "pyproject.toml") {
            inspectPyprojectToml(repoRoot, relPath, fileSet, report);
        }
    }

    if (checkOptionalAdvisoryCache) {
        inspectOptionalAdvisoryCache(repoRoot, report);
    }

    return report;
}

}  // namespace repodoctor
