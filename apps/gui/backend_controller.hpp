#pragma once

#include "repodoctor/config/configuration_manager.hpp"
#include "repodoctor/engine/scan_engine.hpp"
#include "repodoctor/fixes/fix_subsystem.hpp"
#include "repodoctor/reports/report_generator.hpp"

#include <QObject>
#include <QThread>
#include <memory>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace repodoctor::gui {

class BackendController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isScanning READ isScanning NOTIFY scanStateChanged)
    Q_PROPERTY(bool hasScanResult READ hasScanResult NOTIFY scanResultChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString currentRepoName READ currentRepoName NOTIFY scanResultChanged)
    Q_PROPERTY(QString currentRepoPath READ currentRepoPath NOTIFY scanResultChanged)
    Q_PROPERTY(QString scanTimestamp READ scanTimestamp NOTIFY scanResultChanged)
    Q_PROPERTY(double overallScore READ overallScore NOTIFY scanResultChanged)
    Q_PROPERTY(QString overallGrade READ overallGrade NOTIFY scanResultChanged)
    Q_PROPERTY(double scanDurationMs READ scanDurationMs NOTIFY scanResultChanged)
    Q_PROPERTY(int totalFilesDiscovered READ totalFilesDiscovered NOTIFY scanResultChanged)
    Q_PROPERTY(int textFilesAnalyzed READ textFilesAnalyzed NOTIFY scanResultChanged)
    Q_PROPERTY(int binaryFilesDetected READ binaryFilesDetected NOTIFY scanResultChanged)
    Q_PROPERTY(int passedChecks READ passedChecks NOTIFY scanResultChanged)
    Q_PROPERTY(int warningChecks READ warningChecks NOTIFY scanResultChanged)
    Q_PROPERTY(int failedChecks READ failedChecks NOTIFY scanResultChanged)
    Q_PROPERTY(int notApplicableChecks READ notApplicableChecks NOTIFY scanResultChanged)
    Q_PROPERTY(int notVerifiedChecks READ notVerifiedChecks NOTIFY scanResultChanged)
    Q_PROPERTY(int criticalCount READ criticalCount NOTIFY scanResultChanged)
    Q_PROPERTY(int highCount READ highCount NOTIFY scanResultChanged)
    Q_PROPERTY(int mediumCount READ mediumCount NOTIFY scanResultChanged)
    Q_PROPERTY(int lowCount READ lowCount NOTIFY scanResultChanged)
    Q_PROPERTY(int infoCount READ infoCount NOTIFY scanResultChanged)

    Q_PROPERTY(QVariantList categoryScores READ categoryScores NOTIFY scanResultChanged)
    Q_PROPERTY(QVariantList filteredFindings READ filteredFindings NOTIFY findingsFilterChanged)
    Q_PROPERTY(QVariantList plannedFixes READ plannedFixes NOTIFY fixPlanChanged)
    Q_PROPERTY(QVariantList fixExecutionLog READ fixExecutionLog NOTIFY fixExecutionChanged)
    Q_PROPERTY(QVariantList recentScans READ recentScans NOTIFY recentScansChanged)
    Q_PROPERTY(QVariantList exportHistory READ exportHistory NOTIFY exportHistoryChanged)
    Q_PROPERTY(QVariantList rulesCatalog READ rulesCatalog NOTIFY settingsChanged)
    Q_PROPERTY(QString reportPreviewText READ reportPreviewText NOTIFY reportPreviewChanged)

    Q_PROPERTY(int maxFileSizeKb READ maxFileSizeKb WRITE setMaxFileSizeKb NOTIFY settingsChanged)
    Q_PROPERTY(int largeFileThresholdMb READ largeFileThresholdMb WRITE setLargeFileThresholdMb NOTIFY settingsChanged)
    Q_PROPERTY(QString excludePatternsText READ excludePatternsText WRITE setExcludePatternsText NOTIFY settingsChanged)
    Q_PROPERTY(bool enableAdvisoryLookup READ enableAdvisoryLookup WRITE setEnableAdvisoryLookup NOTIFY settingsChanged)
    Q_PROPERTY(bool respectGitignore READ respectGitignore WRITE setRespectGitignore NOTIFY settingsChanged)

public:
    explicit BackendController(QObject* parent = nullptr);
    ~BackendController() override;

    [[nodiscard]] bool isScanning() const noexcept { return isScanning_; }
    [[nodiscard]] bool hasScanResult() const noexcept { return lastResult_.scanCompleted; }
    [[nodiscard]] QString statusMessage() const { return statusMessage_; }
    [[nodiscard]] QString currentRepoName() const;
    [[nodiscard]] QString currentRepoPath() const;
    [[nodiscard]] QString scanTimestamp() const;
    [[nodiscard]] double overallScore() const noexcept {
        return lastResult_.healthScore.overallScore;
    }
    [[nodiscard]] QString overallGrade() const;
    [[nodiscard]] double scanDurationMs() const noexcept {
        return lastResult_.statistics.durationMilliseconds;
    }
    [[nodiscard]] int totalFilesDiscovered() const noexcept {
        return static_cast<int>(lastResult_.statistics.totalFilesDiscovered);
    }
    [[nodiscard]] int textFilesAnalyzed() const noexcept {
        return static_cast<int>(lastResult_.statistics.textFilesAnalyzed);
    }
    [[nodiscard]] int binaryFilesDetected() const noexcept {
        return static_cast<int>(lastResult_.statistics.binaryFilesDetected);
    }
    [[nodiscard]] int passedChecks() const noexcept {
        return static_cast<int>(lastResult_.healthScore.totalPassedChecks);
    }
    [[nodiscard]] int warningChecks() const noexcept {
        return static_cast<int>(lastResult_.healthScore.totalWarningChecks);
    }
    [[nodiscard]] int failedChecks() const noexcept {
        return static_cast<int>(lastResult_.healthScore.totalFailedChecks);
    }
    [[nodiscard]] int notApplicableChecks() const noexcept {
        return static_cast<int>(lastResult_.healthScore.totalNotApplicableChecks);
    }
    [[nodiscard]] int notVerifiedChecks() const noexcept {
        return static_cast<int>(lastResult_.healthScore.totalNotVerifiedChecks);
    }
    [[nodiscard]] int criticalCount() const noexcept { return criticalCount_; }
    [[nodiscard]] int highCount() const noexcept { return highCount_; }
    [[nodiscard]] int mediumCount() const noexcept { return mediumCount_; }
    [[nodiscard]] int lowCount() const noexcept { return lowCount_; }
    [[nodiscard]] int infoCount() const noexcept { return infoCount_; }

    [[nodiscard]] QVariantList categoryScores() const { return categoryScores_; }
    [[nodiscard]] QVariantList filteredFindings() const { return filteredFindings_; }
    [[nodiscard]] QVariantList plannedFixes() const { return plannedFixes_; }
    [[nodiscard]] QVariantList fixExecutionLog() const { return fixExecutionLog_; }
    [[nodiscard]] QVariantList recentScans() const { return recentScans_; }
    [[nodiscard]] QVariantList exportHistory() const { return exportHistory_; }
    [[nodiscard]] QVariantList rulesCatalog() const { return rulesCatalog_; }
    [[nodiscard]] QString reportPreviewText() const { return reportPreviewText_; }

    [[nodiscard]] int maxFileSizeKb() const noexcept {
        return static_cast<int>(config_.maxFileSizeBytes / 1024ULL);
    }
    void setMaxFileSizeKb(int kb);

    [[nodiscard]] int largeFileThresholdMb() const noexcept {
        return static_cast<int>(config_.largeFileThresholdBytes / (1024ULL * 1024ULL));
    }
    void setLargeFileThresholdMb(int mb);

    [[nodiscard]] QString excludePatternsText() const;
    void setExcludePatternsText(const QString& text);

    [[nodiscard]] bool enableAdvisoryLookup() const noexcept {
        return config_.enableAdvisoryLookup;
    }
    void setEnableAdvisoryLookup(bool enabled);

    [[nodiscard]] bool respectGitignore() const noexcept { return config_.respectGitignore; }
    void setRespectGitignore(bool respect);

    Q_INVOKABLE QString browseForRepositoryDirectory();
    Q_INVOKABLE void scanLocalPath(const QString& path);
    Q_INVOKABLE void scanGitHubUrl(const QString& url);
    Q_INVOKABLE void cancelScan();

    Q_INVOKABLE void updateFindingsFilter(
        const QString& searchText,
        const QString& categoryFilter,
        const QString& severityFilter,
        const QString& sortOrder);

    Q_INVOKABLE void openFileLocation(const QString& relativeFilePath);

    Q_INVOKABLE void toggleFixSelection(const QString& fixId, bool selected);
    Q_INVOKABLE void executeSelectedFixes(bool dryRun);

    Q_INVOKABLE void generateReportPreview(const QString& formatName);
    Q_INVOKABLE bool exportReportToFile(const QString& formatName, const QString& filePath);
    Q_INVOKABLE QString browseSaveReportPath(const QString& defaultFilename);

    Q_INVOKABLE void setRuleEnabled(
        const QString& ruleId,
        bool enabled,
        const QString& justification);
    Q_INVOKABLE bool saveConfigurationToRepo();

signals:
    void scanStateChanged();
    void scanResultChanged();
    void statusMessageChanged();
    void findingsFilterChanged();
    void fixPlanChanged();
    void fixExecutionChanged();
    void recentScansChanged();
    void exportHistoryChanged();
    void settingsChanged();
    void reportPreviewChanged();

private:
    void startScan(std::shared_ptr<IRepositorySource> source);
    void applyScanResult(ScanResult result);
    void rebuildFindingsList();
    void rebuildFixPlanList();
    void rebuildRulesCatalog();
    void setStatusMessage(const QString& msg);

    QThread* scanThread_{nullptr};
    std::shared_ptr<IRepositorySource> activeSource_;
    bool isScanning_{false};
    bool settingsEdited_{false};
    std::atomic<bool> cancelRequested_{false};
    QString statusMessage_{"Ready. Select a local repository or enter a public GitHub URL to scan."};

    ScanConfiguration config_{};
    ScanResult lastResult_{};
    FixPlan currentFixPlan_{};
    std::set<std::string> selectedFixIds_;

    int criticalCount_{0};
    int highCount_{0};
    int mediumCount_{0};
    int lowCount_{0};
    int infoCount_{0};

    QString filterSearch_;
    QString filterCategory_{"ALL"};
    QString filterSeverity_{"ALL"};
    QString sortOrder_{"SEVERITY_DESC"};

    QVariantList categoryScores_;
    QVariantList filteredFindings_;
    QVariantList plannedFixes_;
    QVariantList fixExecutionLog_;
    QVariantList recentScans_;
    QVariantList exportHistory_;
    QVariantList rulesCatalog_;
    QString reportPreviewText_;
};

}  // namespace repodoctor::gui
