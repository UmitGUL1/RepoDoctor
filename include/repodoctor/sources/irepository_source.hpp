#pragma once

#include "repodoctor/config/configuration_manager.hpp"

#include <filesystem>
#include <atomic>
#include <memory>
#include <string>

namespace repodoctor {

struct PreparedRepository {
    bool success{false};
    std::filesystem::path localPath;
    std::string displayIdentifier;
    bool isTemporary{false};
    std::string errorMessage;
};

class IRepositorySource {
public:
    virtual ~IRepositorySource() = default;

    [[nodiscard]] virtual std::string sourceDescription() const = 0;
    [[nodiscard]] virtual bool isRemote() const noexcept = 0;
    virtual void setCancellationToken(const std::atomic<bool>* /*token*/) {}
    [[nodiscard]] virtual PreparedRepository prepare(const ScanConfiguration& config) = 0;
};

class LocalRepositorySource final : public IRepositorySource {
public:
    explicit LocalRepositorySource(std::filesystem::path path);
    ~LocalRepositorySource() override = default;

    [[nodiscard]] std::string sourceDescription() const override;
    [[nodiscard]] bool isRemote() const noexcept override { return false; }
    [[nodiscard]] PreparedRepository prepare(const ScanConfiguration& config) override;

private:
    std::filesystem::path targetPath_;
};

class GitHubRepositorySource final : public IRepositorySource {
public:
    explicit GitHubRepositorySource(std::string repoUrl);
    ~GitHubRepositorySource() override;

    GitHubRepositorySource(const GitHubRepositorySource&) = delete;
    GitHubRepositorySource& operator=(const GitHubRepositorySource&) = delete;

    [[nodiscard]] std::string sourceDescription() const override;
    [[nodiscard]] bool isRemote() const noexcept override { return true; }
    [[nodiscard]] PreparedRepository prepare(const ScanConfiguration& config) override;

    void setCancellationToken(const std::atomic<bool>* token) override { cancelToken_ = token; }

    [[nodiscard]] static bool isValidGitHubRepositoryUrl(
        const std::string& url,
        std::string* normalizedUrlOut = nullptr,
        std::string* ownerRepoSlugOut = nullptr,
        std::string* errorReasonOut = nullptr);

private:
    void cleanupTemporaryDirectory() noexcept;

    const std::atomic<bool>* cancelToken_{nullptr};
    std::string rawUrl_;
    std::filesystem::path tempCheckoutDir_;
};

}  // namespace repodoctor
