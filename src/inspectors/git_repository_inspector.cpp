#include "repodoctor/inspectors/git_repository_inspector.hpp"
#include "repodoctor/core/types.hpp"

#ifdef REPODOCTOR_USE_LIBGIT2
#include <git2.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace repodoctor {

namespace {

std::string trimString(std::string_view sv) {
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

std::uint32_t readBigEndianU32(const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

std::uint16_t readBigEndianU16(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8U) | static_cast<std::uint16_t>(bytes[1]));
}

// Parse .git/index (DIRC v2/v3) to extract tracked file paths without spawning external processes
void parseGitIndexFile(
    const std::filesystem::path& indexPath,
    std::set<std::string>& trackedFilesOut) {
    std::ifstream in(indexPath, std::ios::binary);
    if (!in.is_open()) {
        return;
    }
    std::vector<unsigned char> buf(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (buf.size() < 12) {
        return;
    }
    if (buf[0] != 'D' || buf[1] != 'I' || buf[2] != 'R' || buf[3] != 'C') {
        return;
    }
    const std::uint32_t version = readBigEndianU32(&buf[4]);
    if (version != 2 && version != 3) {
        return;
    }
    const std::uint32_t entryCount = readBigEndianU32(&buf[8]);
    std::size_t offset = 12;

    for (std::uint32_t i = 0; i < entryCount && offset + 62 <= buf.size(); ++i) {
        const std::size_t entryStart = offset;
        const std::uint16_t flags = readBigEndianU16(&buf[entryStart + 60]);
        const bool extended = (version >= 3) && ((flags & 0x4000U) != 0U);
        std::size_t nameOffset = entryStart + 62 + (extended ? 2U : 0U);
        if (nameOffset >= buf.size()) {
            break;
        }
        std::size_t nameEnd = nameOffset;
        while (nameEnd < buf.size() && buf[nameEnd] != 0) {
            ++nameEnd;
        }
        if (nameEnd > nameOffset && nameEnd < buf.size()) {
            std::string path(
                reinterpret_cast<const char*>(&buf[nameOffset]), nameEnd - nameOffset);
            trackedFilesOut.insert(path);
        }
        // Entries in v2/v3 are padded with NUL bytes to 8-byte alignment
        const std::size_t rawLen = (nameEnd - entryStart) + 1;
        const std::size_t paddedLen = ((rawLen + 7U) / 8U) * 8U;
        offset = entryStart + paddedLen;
    }
}

void parseGitignoreFile(
    const std::filesystem::path& gitignorePath,
    std::vector<std::string>& patternsOut) {
    std::ifstream in(gitignorePath);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trimString(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }
        patternsOut.push_back(trimmed);
    }
}

void parseGitmodulesFile(
    const std::filesystem::path& gitmodulesPath,
    std::vector<std::string>& submodulePathsOut) {
    std::ifstream in(gitmodulesPath);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trimString(line);
        if (trimmed.rfind("path", 0) == 0) {
            const auto eqPos = trimmed.find('=');
            if (eqPos != std::string::npos) {
                const std::string subPath = trimString(trimmed.substr(eqPos + 1));
                if (!subPath.empty()) {
                    submodulePathsOut.push_back(subPath);
                }
            }
        }
    }
}

}  // namespace

GitRepoMetadata GitRepositoryInspector::inspect(const std::filesystem::path& repoRoot) const {
    GitRepoMetadata meta{};
    std::error_code ec;

    const auto gitignorePath = repoRoot / ".gitignore";
    if (std::filesystem::exists(gitignorePath, ec) &&
        std::filesystem::is_regular_file(gitignorePath, ec)) {
        meta.hasGitignoreFile = true;
        parseGitignoreFile(gitignorePath, meta.gitignorePatterns);
    }

    const auto gitmodulesPath = repoRoot / ".gitmodules";
    if (std::filesystem::exists(gitmodulesPath, ec) &&
        std::filesystem::is_regular_file(gitmodulesPath, ec)) {
        parseGitmodulesFile(gitmodulesPath, meta.submodulePaths);
    }

    const auto dotGitPath = repoRoot / ".git";
    if (!std::filesystem::exists(dotGitPath, ec)) {
        meta.hasGitDirectory = false;
        meta.isValidRepository = false;
        meta.validationError = "No .git directory or gitfile found at repository root.";
        return meta;
    }

    meta.hasGitDirectory = true;

#ifdef REPODOCTOR_USE_LIBGIT2
    struct LibGit2Guard {
        LibGit2Guard() { git_libgit2_init(); }
        ~LibGit2Guard() { git_libgit2_shutdown(); }
    } const libgit2Guard;

    git_repository* repo = nullptr;
    const std::string repoRootUtf8 = pathToUtf8(repoRoot);
    const int openRc = git_repository_open(&repo, repoRootUtf8.c_str());
    if (openRc == 0 && repo != nullptr) {
        meta.isBareRepository = (git_repository_is_bare(repo) != 0);

        git_reference* headSymRef = nullptr;
        const int lookupRc = git_reference_lookup(&headSymRef, repo, "HEAD");
        if (lookupRc == 0 && headSymRef != nullptr) {
            meta.isValidRepository = true;
            if (git_reference_type(headSymRef) == GIT_REFERENCE_SYMBOLIC) {
                const char* target = git_reference_symbolic_target(headSymRef);
                if (target != nullptr) {
                    const std::string targetStr(target);
                    const std::string prefix = "refs/heads/";
                    meta.currentBranch = (targetStr.rfind(prefix, 0) == 0)
                                             ? targetStr.substr(prefix.size())
                                             : targetStr;
                }
            } else {
                meta.headIsDetached = true;
                const git_oid* oid = git_reference_target(headSymRef);
                if (oid != nullptr) {
                    char hexBuf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
                    git_oid_tostr(hexBuf, sizeof(hexBuf), oid);
                    meta.headCommitHash = hexBuf;
                }
            }
            git_reference_free(headSymRef);

            git_index* idx = nullptr;
            if (git_repository_index(&idx, repo) == 0 && idx != nullptr) {
                const std::size_t count = git_index_entrycount(idx);
                for (std::size_t i = 0; i < count; ++i) {
                    const git_index_entry* entry = git_index_get_byindex(idx, i);
                    if (entry != nullptr && entry->path != nullptr) {
                        meta.trackedFiles.insert(entry->path);
                    }
                }
                git_index_free(idx);
            }

            git_repository_free(repo);
            return meta;
        }
        if (headSymRef != nullptr) {
            git_reference_free(headSymRef);
        }
        git_repository_free(repo);
    }
#endif

    // Filesystem inspection of .git directory (also verifies corrupted HEAD or missing subdirs)
    std::filesystem::path resolvedGitDir = dotGitPath;
    if (std::filesystem::is_regular_file(dotGitPath, ec)) {
        std::ifstream gitFile(dotGitPath);
        std::string firstLine;
        std::getline(gitFile, firstLine);
        const std::string trimmed = trimString(firstLine);
        if (trimmed.rfind("gitdir:", 0) == 0) {
            const std::string rel = trimString(trimmed.substr(7));
            resolvedGitDir = repoRoot / rel;
        } else {
            meta.isValidRepository = false;
            meta.headIsCorrupted = true;
            meta.validationError = "Invalid .git file (missing 'gitdir:' prefix).";
            return meta;
        }
    }

    const auto headPath = resolvedGitDir / "HEAD";
    const auto objectsPath = resolvedGitDir / "objects";
    const auto refsPath = resolvedGitDir / "refs";

    if (!std::filesystem::exists(headPath, ec) || !std::filesystem::is_regular_file(headPath, ec)) {
        meta.isValidRepository = false;
        meta.headIsCorrupted = true;
        meta.validationError = "Missing .git/HEAD file.";
        return meta;
    }

    if (!std::filesystem::exists(objectsPath, ec) || !std::filesystem::exists(refsPath, ec)) {
        meta.isValidRepository = false;
        meta.headIsCorrupted = true;
        meta.validationError = "Incomplete .git metadata (missing 'objects' or 'refs' directory).";
        return meta;
    }

    std::ifstream headIn(headPath);
    std::string headContent;
    std::getline(headIn, headContent);
    headContent = trimString(headContent);

    if (headContent.empty()) {
        meta.isValidRepository = false;
        meta.headIsCorrupted = true;
        meta.validationError = ".git/HEAD is empty.";
        return meta;
    }

    if (headContent.rfind("ref:", 0) == 0) {
        const std::string refTarget = trimString(headContent.substr(4));
        if (refTarget.rfind("refs/", 0) != 0 || refTarget.find("..") != std::string::npos) {
            meta.isValidRepository = false;
            meta.headIsCorrupted = true;
            meta.validationError = "Malformed symbolic reference in .git/HEAD: " + refTarget;
            return meta;
        }
        meta.isValidRepository = true;
        const std::string branchPrefix = "refs/heads/";
        if (refTarget.rfind(branchPrefix, 0) == 0) {
            meta.currentBranch = refTarget.substr(branchPrefix.size());
        } else {
            meta.currentBranch = refTarget;
        }
        const auto refFile = resolvedGitDir / refTarget;
        if (std::filesystem::exists(refFile, ec) && std::filesystem::is_regular_file(refFile, ec)) {
            std::ifstream refIn(refFile);
            std::string hashLine;
            std::getline(refIn, hashLine);
            meta.headCommitHash = trimString(hashLine);
        }
    } else if (headContent.size() == 40 || headContent.size() == 64) {
        const bool allHex = std::all_of(headContent.begin(), headContent.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        });
        if (!allHex) {
            meta.isValidRepository = false;
            meta.headIsCorrupted = true;
            meta.validationError = "Corrupted object hash in .git/HEAD.";
            return meta;
        }
        meta.isValidRepository = true;
        meta.headIsDetached = true;
        meta.headCommitHash = headContent;
    } else {
        meta.isValidRepository = false;
        meta.headIsCorrupted = true;
        meta.validationError = "Unrecognized content in .git/HEAD.";
        return meta;
    }

    const auto indexPath = resolvedGitDir / "index";
    if (std::filesystem::exists(indexPath, ec) && std::filesystem::is_regular_file(indexPath, ec)) {
        parseGitIndexFile(indexPath, meta.trackedFiles);
    }

    return meta;
}

bool GitRepositoryInspector::isPathMatchedByGitignore(
    const std::vector<std::string>& gitignorePatterns,
    const std::string& relativePath) {
    for (std::string pat : gitignorePatterns) {
        if (pat.empty() || pat.front() == '!') {
            continue;
        }
        if (pat.front() == '/') {
            pat.erase(pat.begin());
        }
        if (!pat.empty() && pat.back() == '/') {
            const std::string dirPrefix = pat.substr(0, pat.size() - 1);
            if (relativePath == dirPrefix ||
                (relativePath.size() > dirPrefix.size() &&
                 relativePath.substr(0, dirPrefix.size()) == dirPrefix &&
                 relativePath[dirPrefix.size()] == '/')) {
                return true;
            }
        }
        if (pat.size() > 2 && pat.substr(0, 2) == "*.") {
            const std::string ext = pat.substr(1);
            if (relativePath.size() >= ext.size() &&
                relativePath.substr(relativePath.size() - ext.size()) == ext) {
                return true;
            }
        }
        if (relativePath == pat) {
            return true;
        }
    }
    return false;
}

}  // namespace repodoctor
