#include "repodoctor/sources/irepository_source.hpp"

#include "clone_process.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <random>
#include <regex>
#include <sstream>

namespace repodoctor {



LocalRepositorySource::LocalRepositorySource(std::filesystem::path path)
    : targetPath_(std::move(path)) {}

std::string LocalRepositorySource::sourceDescription() const {
    return "Local directory: " + pathToUtf8(targetPath_);
}

PreparedRepository LocalRepositorySource::prepare(const ScanConfiguration& /*config*/) {
    PreparedRepository prep{};
    std::error_code ec;

    if (targetPath_.empty()) {
        prep.errorMessage = "Repository path must not be empty.";
        return prep;
    }

    if (!std::filesystem::exists(targetPath_, ec)) {
        prep.errorMessage = "Repository path does not exist: " + pathToUtf8(targetPath_);
        return prep;
    }

    if (!std::filesystem::is_directory(targetPath_, ec)) {
        prep.errorMessage = "Repository path is not a directory: " + pathToUtf8(targetPath_);
        return prep;
    }

    const auto canonical = std::filesystem::weakly_canonical(targetPath_, ec);
    if (ec) {
        prep.errorMessage =
            "Failed to resolve canonical path for '" + pathToUtf8(targetPath_) + "': " + ec.message();
        return prep;
    }

    prep.success = true;
    prep.localPath = canonical;
    const std::string filenameUtf8 = pathToUtf8(canonical.filename());
    prep.displayIdentifier = filenameUtf8.empty() ? pathToUtf8(canonical) : filenameUtf8;
    prep.isTemporary = false;
    return prep;
}

GitHubRepositorySource::GitHubRepositorySource(std::string repoUrl)
    : rawUrl_(std::move(repoUrl)) {}

GitHubRepositorySource::~GitHubRepositorySource() {
    cleanupTemporaryDirectory();
}

std::string GitHubRepositorySource::sourceDescription() const {
    return "GitHub repository: " + rawUrl_;
}

bool GitHubRepositorySource::isValidGitHubRepositoryUrl(
    const std::string& url,
    std::string* normalizedUrlOut,
    std::string* ownerRepoSlugOut,
    std::string* errorReasonOut) {
    if (url.empty()) {
        if (errorReasonOut != nullptr) {
            *errorReasonOut = "URL must not be empty.";
        }
        return false;
    }

    // Reject option injection or embedded whitespace/control characters
    if (url.front() == '-') {
        if (errorReasonOut != nullptr) {
            *errorReasonOut = "URL must not start with a dash ('-').";
        }
        return false;
    }
    for (char ch : url) {
        const auto c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || ch == ';' || ch == '|' || ch == '&' || ch == '`' || ch == '$' ||
            ch == '\'' || ch == '"') {
            if (errorReasonOut != nullptr) {
                *errorReasonOut = "URL contains disallowed control or shell metacharacters.";
            }
            return false;
        }
    }

    // Strict allowlist: https://github.com/<owner>/<repo>[.git][/]
    static const std::regex kGitHubRegex(
        R"(^https://github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?/?$)");
    std::smatch match;
    if (!std::regex_match(url, match, kGitHubRegex)) {
        if (errorReasonOut != nullptr) {
            *errorReasonOut =
                "Only public HTTPS GitHub URLs of the form 'https://github.com/<owner>/<repo>' are supported.";
        }
        return false;
    }

    const std::string owner = match[1].str();
    const std::string repo = match[2].str();

    if (owner == "." || owner == ".." || repo == "." || repo == "..") {
        if (errorReasonOut != nullptr) {
            *errorReasonOut = "Invalid path traversal segment in GitHub owner or repository name.";
        }
        return false;
    }

    if (normalizedUrlOut != nullptr) {
        *normalizedUrlOut = "https://github.com/" + owner + "/" + repo + ".git";
    }
    if (ownerRepoSlugOut != nullptr) {
        *ownerRepoSlugOut = owner + "/" + repo;
    }
    return true;
}

PreparedRepository GitHubRepositorySource::prepare(const ScanConfiguration& config) {
    PreparedRepository prep{};
    std::string normalizedUrl;
    std::string slug;
    std::string validationError;

    if (!isValidGitHubRepositoryUrl(rawUrl_, &normalizedUrl, &slug, &validationError)) {
        prep.errorMessage = validationError;
        return prep;
    }

    cleanupTemporaryDirectory();

    std::error_code ec;
    const auto tempBase = std::filesystem::temp_directory_path(ec);
    if (ec) {
        prep.errorMessage = "Unable to locate system temporary directory: " + ec.message();
        return prep;
    }

    std::random_device entropy;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto candidate = tempBase / ("repodoctor_gh_" + std::to_string(entropy()) +
            "_" + std::to_string(entropy()));
        if (std::filesystem::create_directory(candidate, ec)) {
            tempCheckoutDir_ = candidate;
            break;
        }
        if (ec) break;
    }
    if (tempCheckoutDir_.empty()) {
        prep.errorMessage = "Failed to create a unique temporary checkout directory.";
        return prep;
    }

    const std::string cloneError = detail::cloneRepository(normalizedUrl, tempCheckoutDir_,
        config.cloneTimeoutSeconds, config.maxCloneSizeBytes, cancelToken_);
    if (!cloneError.empty()) {
        cleanupTemporaryDirectory();
        prep.errorMessage = cloneError;
        return prep;
    }

    prep.success = true;
    prep.localPath = tempCheckoutDir_;
    prep.displayIdentifier = slug;
    prep.isTemporary = true;
    return prep;
}

void GitHubRepositorySource::cleanupTemporaryDirectory() noexcept {
    if (!tempCheckoutDir_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(tempCheckoutDir_, ec);
        tempCheckoutDir_.clear();
    }
}

}  // namespace repodoctor
