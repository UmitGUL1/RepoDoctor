#include "repodoctor/engine/scan_context.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace repodoctor {

namespace {

std::string toLowerAscii(std::string_view sv) {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool hasKnownBinaryExtension(const std::filesystem::path& path) {
    static const std::array<std::string_view, 36> kBinExts = {
        ".png",  ".jpg",  ".jpeg", ".gif",  ".bmp",  ".ico",  ".webp", ".pdf",  ".zip",
        ".tar",  ".gz",   ".7z",   ".rar",  ".xz",   ".exe",  ".dll",  ".so",   ".dylib",
        ".a",    ".lib",  ".o",    ".obj",  ".pdb",  ".pyc",  ".pyo",  ".pyd",  ".class",
        ".jar",  ".war",  ".wasm", ".ttf",  ".otf",  ".woff", ".woff2",".mp3",  ".mp4"
    };
    const std::string ext = toLowerAscii(pathToUtf8(path.extension()));
    for (std::string_view binExt : kBinExts) {
        if (ext == binExt) {
            return true;
        }
    }
    return false;
}

}  // namespace

ScanContext::ScanContext(
    std::filesystem::path repoRoot,
    ScanConfiguration config,
    const std::atomic<bool>* cancelToken,
    ScanProgressCallback progressCallback)
    : repoRoot_(std::move(repoRoot)),
      config_(std::move(config)),
      cancelToken_(cancelToken),
      progressCallback_(std::move(progressCallback)) {}

bool ScanContext::isCancelled() const noexcept {
    return cancelToken_ != nullptr && cancelToken_->load(std::memory_order_relaxed);
}

bool ScanContext::detectBinaryBuffer(std::string_view buffer) {
    if (buffer.empty()) {
        return false;
    }
    const std::size_t sampleSize = std::min<std::size_t>(buffer.size(), 8192);
    std::size_t suspiciousControlCount = 0;
    for (std::size_t i = 0; i < sampleSize; ++i) {
        const unsigned char c = static_cast<unsigned char>(buffer[i]);
        if (c == 0) {
            return true;  // NUL byte in first 8 KB strongly indicates binary content
        }
        if (c < 0x09 || (c > 0x0D && c < 0x20 && c != 0x1B)) {
            ++suspiciousControlCount;
        }
    }
    return (suspiciousControlCount * 10 > sampleSize * 3);
}

void ScanContext::indexRepository() {
    const auto startClock = std::chrono::steady_clock::now();

    if (progressCallback_) {
        progressCallback_("Inspecting Git metadata", 0, 4);
    }

    GitRepositoryInspector gitInspector;
    gitMetadata_ = gitInspector.inspect(repoRoot_);

    if (isCancelled()) {
        stats_.wasCancelled = true;
        return;
    }

    if (progressCallback_) {
        progressCallback_("Indexing repository files", 1, 4);
    }

    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(repoRoot_, ec);
    const std::filesystem::path baseRoot = ec ? repoRoot_ : canonicalRoot;

    std::set<std::string> incrementalFilter;
    const bool useIncrementalFilter = isIncrementalScan();
    if (useIncrementalFilter) {
        if (config_.stagedOnly) {
            const auto staged = gitInspector.getStagedFiles(baseRoot, cancelToken_);
            incrementalFilter.insert(staged.begin(), staged.end());
        }
        if (!config_.sinceGitRef.empty()) {
            const auto changed = gitInspector.getChangedFilesSince(baseRoot, config_.sinceGitRef, cancelToken_);
            incrementalFilter.insert(changed.begin(), changed.end());
        }
    }

    std::filesystem::recursive_directory_iterator it(
        baseRoot, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;

    SecretDetectionEngine secretEngine;
    std::vector<std::string> allRelativePaths;


    if (config_.stagedOnly) {
        for (const auto& relStr : incrementalFilter) {
            if (isCancelled()) { stats_.wasCancelled = true; break; }
            if (ConfigurationManager::matchesExclusion(config_, relStr)) { ++stats_.skippedByExclusion; continue; }
            if (++stats_.totalFilesDiscovered > config_.maxRepositoryScanFiles) { stats_.fileLimitReached = true; break; }
            IndexedFile file{};
            file.relativePath = relStr; file.absolutePath = baseRoot / utf8ToPath(relStr); file.isTrackedInGit = true;
            file.sizeBytes = gitInspector.stagedFileSize(baseRoot, relStr, cancelToken_);
            if (file.sizeBytes > config_.maxFileSizeBytes) ++stats_.skippedBySizeLimit;
            else {
                if (file.sizeBytes > 32 * 1024 * 1024 - stats_.totalBytesRead)
                    throw std::runtime_error("Staged content byte limit exceeded.");
                auto content = gitInspector.readStagedFile(baseRoot, relStr, static_cast<std::size_t>(config_.maxFileSizeBytes), cancelToken_);
                stats_.totalBytesRead += content.size();
                file.isBinary = detectBinaryBuffer(content) || hasKnownBinaryExtension(utf8ToPath(relStr));
                if (file.isBinary) ++stats_.binaryFilesDetected;
                else {
                    ++stats_.textFilesAnalyzed;
                    auto matches = secretEngine.scanFileContent(relStr, content, &stats_.suppressedByComment);
                    for (auto& match : matches) secretMatches_.push_back(std::move(match));
                    stagedContents_[relStr] = std::move(content);
                }
            }
            allRelativePaths.push_back(relStr); files_.push_back(std::move(file));
        }
        it = end; // Index entries may exist even when the working file was deleted.
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (isCancelled()) {
            stats_.wasCancelled = true;
            break;
        }

        const auto& entryPath = it->path();
        // Inspect the directory entry itself before any operation can resolve its target.
        std::error_code symEc;
        const auto symStatus = std::filesystem::symlink_status(entryPath, symEc);
        if (!symEc && std::filesystem::is_symlink(symStatus)) {
            ++stats_.symlinksSkipped;
            it.disable_recursion_pending();
            continue;
        }
        const auto relFsPath = entryPath.lexically_relative(baseRoot);
        if (relFsPath.empty()) continue;
        const std::string relStr = pathToGenericUtf8(relFsPath);
        std::error_code relEc;

        // Never descend into .git internal object database or backup directories
        if (relStr == ".git" || relStr.rfind(".git/", 0) == 0 ||
            relStr == ".repodoctor-backup" || relStr.rfind(".repodoctor-backup/", 0) == 0) {
            if (it->is_directory(relEc)) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (ConfigurationManager::matchesExclusion(config_, relStr)) {
            stats_.skippedByExclusion++;
            if (it->is_directory(symEc)) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (config_.respectGitignore &&
            GitRepositoryInspector::isPathMatchedByGitignore(
                gitMetadata_.gitignorePatterns, relStr)) {
            stats_.skippedByExclusion++;
            if (it->is_directory(symEc)) {
                it.disable_recursion_pending();
            }
            continue;
        }

        std::error_code regEc;
        if (!it->is_regular_file(regEc) || regEc) {
            continue;
        }

        if (useIncrementalFilter && incrementalFilter.count(relStr) == 0) {
            continue;
        }

        stats_.totalFilesDiscovered++;
        if (stats_.totalFilesDiscovered > config_.maxRepositoryScanFiles) {
            stats_.fileLimitReached = true;
            break;
        }

        IndexedFile indexed{};
        indexed.relativePath = relStr;
        indexed.absolutePath = entryPath;
        std::error_code sizeEc;
        indexed.sizeBytes = it->file_size(sizeEc);
        if (sizeEc) {
            indexed.sizeBytes = 0;
        }
        indexed.isTrackedInGit = (gitMetadata_.trackedFiles.count(relStr) > 0);

        std::error_code permEc;
        const auto status = std::filesystem::status(entryPath, permEc);
        if (!permEc) {
            const auto perms = status.permissions();
            indexed.posixMode = static_cast<std::uint32_t>(perms) & 0777U;
            indexed.isWorldWritable =
                ((perms & std::filesystem::perms::others_write) != std::filesystem::perms::none);
            indexed.isGroupOrWorldReadable =
                ((perms & (std::filesystem::perms::group_read |
                           std::filesystem::perms::others_read)) != std::filesystem::perms::none);
            indexed.isExecutable =
                ((perms & (std::filesystem::perms::owner_exec |
                           std::filesystem::perms::group_exec |
                           std::filesystem::perms::others_exec)) != std::filesystem::perms::none);
        }

        if (hasKnownBinaryExtension(entryPath)) {
            indexed.isBinary = true;
            stats_.binaryFilesDetected++;
        } else {
            // Read up to 8 KB header to detect binary content
            std::ifstream headerStream(entryPath, std::ios::binary);
            if (headerStream.is_open()) {
                std::array<char, 8192> headerBuf{};
                headerStream.read(headerBuf.data(), static_cast<std::streamsize>(headerBuf.size()));
                const auto readCount = static_cast<std::size_t>(headerStream.gcount());
                if (detectBinaryBuffer(std::string_view(headerBuf.data(), readCount))) {
                    indexed.isBinary = true;
                    stats_.binaryFilesDetected++;
                }
            }
        }

        if (!indexed.isBinary) {
            if (indexed.sizeBytes > config_.maxFileSizeBytes) {
                stats_.skippedBySizeLimit++;
            } else {
                stats_.textFilesAnalyzed++;
                std::ifstream textStream(entryPath, std::ios::binary);
                if (textStream.is_open()) {
                    std::string content(
                        static_cast<std::size_t>(indexed.sizeBytes), '\0');
                    textStream.read(
                        content.data(), static_cast<std::streamsize>(indexed.sizeBytes));
                    const auto actualRead = static_cast<std::size_t>(textStream.gcount());
                    content.resize(actualRead);
                    stats_.totalBytesRead += actualRead;

                    auto fileSecrets = secretEngine.scanFileContent(
                        relStr, content, &stats_.suppressedByComment);
                    for (auto& s : fileSecrets) {
                        secretMatches_.push_back(std::move(s));
                    }
                }
            }
        }

        allRelativePaths.push_back(relStr);
        fileIndexByPath_[relStr] = files_.size();
        files_.push_back(std::move(indexed));
    }

    // Optional Git commit history inspection (--scan-history)
    if (!isCancelled() && config_.scanHistory) {
        historicalAdditions_ = gitInspector.inspectCommitHistory(
            baseRoot, config_.maxHistoryCommits, &stats_.commitsScanned, cancelToken_,
            static_cast<std::size_t>(config_.maxFileSizeBytes));
        for (const auto& hist : historicalAdditions_) {
            if (ConfigurationManager::matchesExclusion(config_, hist.relativeFilePath)) {
                continue;
            }
            if (hist.addedSizeBytes > config_.maxFileSizeBytes) ++stats_.skippedBySizeLimit;
            if (!hist.addedContent.empty() && !detectBinaryBuffer(hist.addedContent)) {
                auto histSecrets = secretEngine.scanFileContent(
                    hist.relativeFilePath, hist.addedContent, &stats_.suppressedByComment);
                for (auto& s : histSecrets) {
                    s.commitHash = hist.commitHash;
                    s.commitAuthor = hist.commitAuthor;
                    s.commitMessage = hist.commitMessage;
                    secretMatches_.push_back(std::move(s));
                }
            }
        }
    }

    // Keep files sorted deterministically by relativePath
    std::sort(files_.begin(), files_.end(), [](const IndexedFile& a, const IndexedFile& b) {
        return a.relativePath < b.relativePath;
    });
    fileIndexByPath_.clear();
    for (std::size_t i = 0; i < files_.size(); ++i) {
        fileIndexByPath_[files_[i].relativePath] = i;
    }

    if (!isCancelled() && !isIncrementalScan()) {
        if (progressCallback_) {
            progressCallback_("Inspecting dependency manifests", 2, 4);
        }
        DependencyInspector depInspector;
        dependencyReport_ = depInspector.inspect(
            baseRoot, allRelativePaths, config_.enableAdvisoryLookup);
    }

    const auto endClock = std::chrono::steady_clock::now();
    stats_.durationMilliseconds =
        std::chrono::duration<double, std::milli>(endClock - startClock).count();
}

bool ScanContext::checkAndRecordInlineSuppression(
    std::string_view line,
    std::string_view ruleId) const {
    if (SecretDetectionEngine::hasInlineSuppression(line, ruleId)) {
        stats_.suppressedByComment++;
        return true;
    }
    return false;
}

bool ScanContext::hasFile(const std::string& relativePath) const {
    return fileIndexByPath_.find(relativePath) != fileIndexByPath_.end();
}

std::optional<IndexedFile> ScanContext::findFileCaseInsensitive(
    const std::string& relativePath) const {
    const std::string targetLower = toLowerAscii(relativePath);
    for (const auto& f : files_) {
        if (toLowerAscii(f.relativePath) == targetLower) {
            return f;
        }
    }
    return std::nullopt;
}

std::optional<std::string> ScanContext::readTextFile(
    const std::string& relativePath,
    std::size_t maxBytes) const {
    const auto it = fileIndexByPath_.find(relativePath);
    if (it == fileIndexByPath_.end()) {
        return std::nullopt;
    }
    const IndexedFile& info = files_[it->second];
    if (info.isBinary) {
        return std::nullopt;
    }
    const std::size_t limit =
        (maxBytes > 0) ? maxBytes : static_cast<std::size_t>(config_.maxFileSizeBytes);
    if (info.sizeBytes > limit) {
        return std::nullopt;
    }
    if (config_.stagedOnly) {
        const auto cached = stagedContents_.find(relativePath);
        return cached == stagedContents_.end() ? std::nullopt : std::optional<std::string>(cached->second);
    }
    std::ifstream in(info.absolutePath, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }
    std::string data(static_cast<std::size_t>(info.sizeBytes), '\0');
    in.read(data.data(), static_cast<std::streamsize>(info.sizeBytes));
    data.resize(static_cast<std::size_t>(in.gcount()));
    return data;
}

}  // namespace repodoctor
