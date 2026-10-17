#include "backend_controller.hpp"

#include "repodoctor/sources/irepository_source.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <sstream>

namespace repodoctor::gui {

BackendController::BackendController(QObject* parent) : QObject(parent) {
    rebuildRulesCatalog();
}

QString BackendController::currentRepoName() const {
    return QString::fromStdString(lastResult_.repositoryName);
}

QString BackendController::currentRepoPath() const {
    return QString::fromStdString(lastResult_.repositoryPath);
}

QString BackendController::scanTimestamp() const {
    return QString::fromStdString(lastResult_.scanTimestampIso8601);
}

QString BackendController::overallGrade() const {
    return QString::fromStdString(lastResult_.healthScore.gradeLabel);
}

void BackendController::setMaxFileSizeKb(int kb) {
    settingsEdited_ = true;
    if (kb > 0) {
        config_.maxFileSizeBytes = static_cast<std::uint64_t>(kb) * 1024ULL;
        emit settingsChanged();
    }
}

void BackendController::setLargeFileThresholdMb(int mb) {
    settingsEdited_ = true;
    if (mb > 0) {
        config_.largeFileThresholdBytes =
            static_cast<std::uint64_t>(mb) * 1024ULL * 1024ULL;
        emit settingsChanged();
    }
}

QString BackendController::excludePatternsText() const {
    QStringList list;
    for (const auto& p : config_.excludePatterns) {
        list.append(QString::fromStdString(p));
    }
    return list.join(", ");
}

void BackendController::setExcludePatternsText(const QString& text) {
    settingsEdited_ = true;
    config_.excludePatterns.clear();
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        const QString trimmed = p.trimmed();
        if (!trimmed.isEmpty()) {
            config_.excludePatterns.push_back(trimmed.toStdString());
        }
    }
    emit settingsChanged();
}

void BackendController::setEnableAdvisoryLookup(bool enabled) {
    settingsEdited_ = true;
    if (config_.enableAdvisoryLookup != enabled) {
        config_.enableAdvisoryLookup = enabled;
        emit settingsChanged();
    }
}

void BackendController::setRespectGitignore(bool respect) {
    settingsEdited_ = true;
    if (config_.respectGitignore != respect) {
        config_.respectGitignore = respect;
        emit settingsChanged();
    }
}

QString BackendController::browseForRepositoryDirectory() {
    return QFileDialog::getExistingDirectory(
        nullptr,
        QStringLiteral("Select Git Repository Directory"),
        QString());
}

BackendController::~BackendController() {
    cancelRequested_.store(true);
    if (scanThread_) scanThread_->wait();
}

void BackendController::scanLocalPath(const QString& path) {
    if (isScanning_) return;
    if (path.trimmed().isEmpty()) {
        setStatusMessage(QStringLiteral("Please provide a local repository directory."));
        return;
    }
    if (!settingsEdited_) {
        config_ = ConfigurationManager{}.loadFromRepository(utf8ToPath(path.trimmed().toStdString()));
        rebuildRulesCatalog();
        emit settingsChanged();
    }
    startScan(std::make_shared<LocalRepositorySource>(utf8ToPath(path.trimmed().toStdString())));
}

void BackendController::scanGitHubUrl(const QString& url) {
    if (isScanning_) return;
    if (url.trimmed().isEmpty()) {
        setStatusMessage(QStringLiteral("Please provide a public GitHub URL."));
        return;
    }
    startScan(std::make_shared<GitHubRepositorySource>(url.trimmed().toStdString()));
}

void BackendController::startScan(std::shared_ptr<IRepositorySource> source) {
    if (isScanning_) return;
    isScanning_ = true;
    cancelRequested_.store(false);
    emit scanStateChanged();
    setStatusMessage(QStringLiteral("Scanning repository..."));
    const auto config = config_;
    auto result = std::make_shared<ScanResult>();
    auto* worker = QThread::create([this, source, config, result]() {
        try {
            source->setCancellationToken(&cancelRequested_);
            *result = ScanEngine{}.scan(*source, config, &cancelRequested_);
        } catch (const std::exception& ex) {
            result->scanCompleted = false;
            result->errorMessage = ex.what();
        } catch (...) {
            result->scanCompleted = false;
            result->errorMessage = "Unexpected scan failure.";
        }
    });
    worker->setParent(this);
    scanThread_ = worker;
    connect(worker, &QThread::finished, this, [this, source, result, worker]() {
        scanThread_ = nullptr;
        if (result->scanCompleted &&
            !(activeSource_ && activeSource_->isRemote() && !source->isRemote() &&
              result->repositoryPath == lastResult_.repositoryPath)) {
            activeSource_ = source;
        }
        isScanning_ = false;
        applyScanResult(std::move(*result));
        emit scanStateChanged();
        worker->deleteLater();
    });
    worker->start();
}

void BackendController::cancelScan() {
    cancelRequested_.store(true);
    setStatusMessage(QStringLiteral("Cancelling scan..."));
}

void BackendController::applyScanResult(ScanResult result) {
    if (!result.scanCompleted) {
        setStatusMessage(
            QStringLiteral("Scan failed: ") + QString::fromStdString(result.errorMessage));
        return;
    }

    lastResult_ = std::move(result);

    criticalCount_ = 0;
    highCount_ = 0;
    mediumCount_ = 0;
    lowCount_ = 0;
    infoCount_ = 0;

    for (const auto& f : lastResult_.findings) {
        switch (f.severity) {
            case Severity::Critical:
                ++criticalCount_;
                break;
            case Severity::High:
                ++highCount_;
                break;
            case Severity::Medium:
                ++mediumCount_;
                break;
            case Severity::Low:
                ++lowCount_;
                break;
            case Severity::Info:
                ++infoCount_;
                break;
        }
    }

    categoryScores_.clear();
    for (const auto& [cat, cs] : lastResult_.healthScore.categoryScores) {
        QVariantMap map;
        map[QStringLiteral("categoryKey")] = QString::fromStdString(toString(cat));
        map[QStringLiteral("label")] = QString::fromStdString(toDisplayLabel(cat));
        map[QStringLiteral("score")] = cs.score;
        map[QStringLiteral("status")] = QString::fromStdString(toString(cs.status));
        map[QStringLiteral("weight")] = cs.effectiveNormalizedWeight;
        map[QStringLiteral("passed")] = static_cast<int>(cs.passedChecks);
        map[QStringLiteral("warnings")] = static_cast<int>(cs.warningChecks);
        map[QStringLiteral("failed")] = static_cast<int>(cs.failedChecks);
        map[QStringLiteral("notApplicable")] = static_cast<int>(cs.notApplicableChecks);
        map[QStringLiteral("notVerified")] = static_cast<int>(cs.notVerifiedChecks);
        categoryScores_.append(map);
    }

    // Record in recent scans history
    QVariantMap recentEntry;
    recentEntry[QStringLiteral("name")] = QString::fromStdString(lastResult_.repositoryName);
    recentEntry[QStringLiteral("path")] = QString::fromStdString(lastResult_.repositoryPath);
    recentEntry[QStringLiteral("score")] = lastResult_.healthScore.overallScore;
    recentEntry[QStringLiteral("grade")] =
        QString::fromStdString(lastResult_.healthScore.gradeLabel);
    recentEntry[QStringLiteral("findingsCount")] = static_cast<int>(lastResult_.findings.size());
    recentEntry[QStringLiteral("timestamp")] =
        QString::fromStdString(lastResult_.scanTimestampIso8601);

    for (int i = 0; i < recentScans_.size(); ++i) {
        if (recentScans_[i].toMap().value(QStringLiteral("path")).toString() ==
            recentEntry[QStringLiteral("path")].toString()) {
            recentScans_.removeAt(i);
            break;
        }
    }
    recentScans_.prepend(recentEntry);
    while (recentScans_.size() > 10) {
        recentScans_.removeLast();
    }
    emit recentScansChanged();

    rebuildFindingsList();
    rebuildFixPlanList();
    generateReportPreview(QStringLiteral("json"));

    emit scanResultChanged();
    setStatusMessage(
        QStringLiteral("Scan complete: ") + QString::fromStdString(lastResult_.repositoryName) +
        QStringLiteral(" — Score ") +
        QString::number(lastResult_.healthScore.overallScore, 'f', 1) +
        QStringLiteral("/100 (") + QString::number(lastResult_.findings.size()) +
        QStringLiteral(" finding(s))"));
}

void BackendController::updateFindingsFilter(
    const QString& searchText,
    const QString& categoryFilter,
    const QString& severityFilter,
    const QString& sortOrder) {
    filterSearch_ = searchText.trimmed();
    filterCategory_ = categoryFilter.isEmpty() ? QStringLiteral("ALL") : categoryFilter;
    filterSeverity_ = severityFilter.isEmpty() ? QStringLiteral("ALL") : severityFilter;
    sortOrder_ = sortOrder.isEmpty() ? QStringLiteral("SEVERITY_DESC") : sortOrder;
    rebuildFindingsList();
}

void BackendController::rebuildFindingsList() {
    std::vector<Finding> working = lastResult_.findings;

    if (sortOrder_ == QStringLiteral("SEVERITY_ASC")) {
        std::sort(working.begin(), working.end(), [](const Finding& a, const Finding& b) {
            if (a.severity != b.severity) {
                return static_cast<int>(a.severity) < static_cast<int>(b.severity);
            }
            return a.ruleId < b.ruleId;
        });
    } else if (sortOrder_ == QStringLiteral("RULE_ID")) {
        std::sort(working.begin(), working.end(), [](const Finding& a, const Finding& b) {
            return a.ruleId < b.ruleId;
        });
    } else {
        std::sort(working.begin(), working.end(), [](const Finding& a, const Finding& b) {
            if (a.severity != b.severity) {
                return static_cast<int>(a.severity) > static_cast<int>(b.severity);
            }
            return a.ruleId < b.ruleId;
        });
    }

    filteredFindings_.clear();
    const QString lowerSearch = filterSearch_.toLower();

    for (const auto& f : working) {
        const QString catStr = QString::fromStdString(toString(f.category));
        const QString sevStr = QString::fromStdString(toString(f.severity));

        if (filterCategory_ != QStringLiteral("ALL") && catStr != filterCategory_) {
            continue;
        }
        if (filterSeverity_ != QStringLiteral("ALL") && sevStr != filterSeverity_) {
            continue;
        }

        if (!lowerSearch.isEmpty()) {
            const QString idQ = QString::fromStdString(f.ruleId).toLower();
            const QString titleQ = QString::fromStdString(f.title).toLower();
            const QString sumQ = QString::fromStdString(f.summary).toLower();
            bool matchEvidence = false;
            for (const auto& ev : f.evidence) {
                if (QString::fromStdString(ev.relativeFilePath).toLower().contains(lowerSearch) ||
                    QString::fromStdString(ev.snippet).toLower().contains(lowerSearch)) {
                    matchEvidence = true;
                    break;
                }
            }
            if (!idQ.contains(lowerSearch) && !titleQ.contains(lowerSearch) &&
                !sumQ.contains(lowerSearch) && !matchEvidence) {
                continue;
            }
        }

        QVariantList evList;
        for (const auto& e : f.evidence) {
            QVariantMap evMap;
            evMap[QStringLiteral("file")] = QString::fromStdString(e.relativeFilePath);
            evMap[QStringLiteral("line")] = static_cast<int>(e.lineNumber);
            evMap[QStringLiteral("column")] = static_cast<int>(e.columnNumber);
            evMap[QStringLiteral("snippet")] = QString::fromStdString(e.snippet);
            evMap[QStringLiteral("context")] = QString::fromStdString(e.contextNote);
            evMap[QStringLiteral("isRedacted")] = e.isRedacted;
            evList.append(evMap);
        }

        QVariantMap item;
        item[QStringLiteral("ruleId")] = QString::fromStdString(f.ruleId);
        item[QStringLiteral("title")] = QString::fromStdString(f.title);
        item[QStringLiteral("category")] = catStr;
        item[QStringLiteral("categoryLabel")] = QString::fromStdString(toDisplayLabel(f.category));
        item[QStringLiteral("severity")] = sevStr;
        item[QStringLiteral("status")] = QString::fromStdString(toString(f.status));
        item[QStringLiteral("summary")] = QString::fromStdString(f.summary);
        item[QStringLiteral("description")] = QString::fromStdString(f.description);
        item[QStringLiteral("remediation")] = QString::fromStdString(f.remediationGuidance);
        item[QStringLiteral("falsePositives")] = QString::fromStdString(f.falsePositiveNotes);
        item[QStringLiteral("fixable")] = f.fixable;
        item[QStringLiteral("fixId")] = QString::fromStdString(f.fixId);
        item[QStringLiteral("primaryFile")] =
            f.evidence.empty() ? QStringLiteral(".")
                               : QString::fromStdString(f.evidence.front().relativeFilePath);
        item[QStringLiteral("evidence")] = evList;
        filteredFindings_.append(item);
    }

    emit findingsFilterChanged();
}

void BackendController::openFileLocation(const QString& relativeFilePath) {
    if (!lastResult_.scanCompleted) {
        return;
    }
    const std::filesystem::path root(lastResult_.repositoryPath);
    const std::filesystem::path target =
        relativeFilePath.isEmpty() ? root : (root / relativeFilePath.toStdString());
    const std::filesystem::path dirToOpen =
        std::filesystem::is_directory(target) ? target : target.parent_path();
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromStdString(dirToOpen.generic_string())));
}

void BackendController::rebuildFixPlanList() {
    plannedFixes_.clear();
    selectedFixIds_.clear();

    if (!lastResult_.scanCompleted) {
        emit fixPlanChanged();
        return;
    }

    FixPlanner planner;
    currentFixPlan_ = planner.createPlan(lastResult_, lastResult_.repositoryPath);

    for (const auto& fix : currentFixPlan_.fixes) {
        if (!fix.hasConflict) {
            selectedFixIds_.insert(fix.fixId);
        }
        QVariantMap map;
        map[QStringLiteral("fixId")] = QString::fromStdString(fix.fixId);
        map[QStringLiteral("ruleId")] = QString::fromStdString(fix.ruleId);
        map[QStringLiteral("title")] = QString::fromStdString(fix.title);
        map[QStringLiteral("rationale")] = QString::fromStdString(fix.rationale);
        map[QStringLiteral("targetPath")] = QString::fromStdString(fix.targetRelativePath);
        map[QStringLiteral("unifiedDiff")] = QString::fromStdString(fix.unifiedDiff);
        map[QStringLiteral("hasConflict")] = fix.hasConflict;
        map[QStringLiteral("conflictReason")] = QString::fromStdString(fix.conflictReason);
        map[QStringLiteral("selected")] = !fix.hasConflict;
        plannedFixes_.append(map);
    }

    emit fixPlanChanged();
}

void BackendController::toggleFixSelection(const QString& fixId, bool selected) {
    const std::string idStr = fixId.toStdString();
    if (selected) {
        selectedFixIds_.insert(idStr);
    } else {
        selectedFixIds_.erase(idStr);
    }
    for (int i = 0; i < plannedFixes_.size(); ++i) {
        QVariantMap map = plannedFixes_[i].toMap();
        if (map[QStringLiteral("fixId")].toString() == fixId) {
            map[QStringLiteral("selected")] = selected;
            plannedFixes_[i] = map;
            break;
        }
    }
    emit fixPlanChanged();
}

void BackendController::executeSelectedFixes(bool dryRun) {
    if (isScanning_ || !lastResult_.scanCompleted || currentFixPlan_.fixes.empty()) {
        return;
    }

    if (selectedFixIds_.empty()) {
        setStatusMessage(QStringLiteral("Select at least one fix to continue."));
        return;
    }
    FixExecutor executor;
    const FixExecutionReport rep =
        executor.execute(currentFixPlan_, selectedFixIds_, dryRun, true);

    fixExecutionLog_.clear();
    for (const auto& item : rep.results) {
        if (item.skipped) {
            continue;
        }
        QVariantMap logEntry;
        logEntry[QStringLiteral("fixId")] = QString::fromStdString(item.fixId);
        logEntry[QStringLiteral("targetPath")] = QString::fromStdString(item.targetRelativePath);
        logEntry[QStringLiteral("applied")] = item.applied;
        logEntry[QStringLiteral("dryRun")] = dryRun;
        logEntry[QStringLiteral("backupPath")] = QString::fromStdString(item.backupRelativePath);
        logEntry[QStringLiteral("message")] = QString::fromStdString(item.message);
        fixExecutionLog_.append(logEntry);
    }
    emit fixExecutionChanged();

    if (!dryRun && rep.appliedCount > 0) {
        // Re-scan repository so dashboard and findings immediately reflect applied improvements
        scanLocalPath(QString::fromStdString(lastResult_.repositoryPath));
    }
}

void BackendController::generateReportPreview(const QString& formatName) {
    if (!lastResult_.scanCompleted) {
        reportPreviewText_ = QStringLiteral("Run a repository scan first to generate reports.");
        emit reportPreviewChanged();
        return;
    }

    const auto fmt = parseReportFormat(formatName.toStdString()).value_or(ReportFormat::Json);
    ReportGenerator gen;
    reportPreviewText_ = QString::fromStdString(gen.generate(lastResult_, fmt));
    emit reportPreviewChanged();
}

QString BackendController::browseSaveReportPath(const QString& defaultFilename) {
    return QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Export RepoDoctor Report"), defaultFilename);
}

bool BackendController::exportReportToFile(const QString& formatName, const QString& filePath) {
    if (!lastResult_.scanCompleted || filePath.trimmed().isEmpty()) {
        return false;
    }

    const auto fmt = parseReportFormat(formatName.toStdString()).value_or(ReportFormat::Json);
    ReportGenerator gen;
    std::string err;
    const bool ok = gen.writeToFile(lastResult_, fmt, filePath.trimmed().toStdString(), &err);

    QVariantMap entry;
    entry[QStringLiteral("timestamp")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    entry[QStringLiteral("format")] = formatName.toUpper();
    entry[QStringLiteral("path")] = filePath.trimmed();
    entry[QStringLiteral("status")] = ok ? QStringLiteral("Exported")
                                         : (QStringLiteral("Error: ") + QString::fromStdString(err));
    exportHistory_.prepend(entry);
    emit exportHistoryChanged();

    setStatusMessage(
        ok ? (QStringLiteral("Exported ") + formatName.toUpper() + QStringLiteral(" report to ") +
              filePath)
           : (QStringLiteral("Failed to export report: ") + QString::fromStdString(err)));
    return ok;
}

void BackendController::rebuildRulesCatalog() {
    rulesCatalog_.clear();
    const auto registry = RuleRegistry::createDefaultRegistry();
    for (const auto& rule : registry.rules()) {
        const std::string idStr = rule->id();
        const bool enabled = ConfigurationManager::isRuleEnabled(config_, idStr);
        std::string justification;
        const auto it = config_.disabledRules.find(idStr);
        if (it != config_.disabledRules.end()) {
            justification = it->second.justification;
        }

        QVariantMap item;
        item[QStringLiteral("ruleId")] = QString::fromStdString(idStr);
        item[QStringLiteral("title")] = QString::fromStdString(rule->title());
        item[QStringLiteral("category")] =
            QString::fromStdString(toDisplayLabel(rule->category()));
        item[QStringLiteral("severity")] =
            QString::fromStdString(toString(rule->defaultSeverity()));
        item[QStringLiteral("description")] = QString::fromStdString(rule->description());
        item[QStringLiteral("fixable")] = rule->hasSafeFix();
        item[QStringLiteral("enabled")] = enabled;
        item[QStringLiteral("justification")] = QString::fromStdString(justification);
        rulesCatalog_.append(item);
    }
}

void BackendController::setRuleEnabled(
    const QString& ruleId,
    bool enabled,
    const QString& justification) {
    settingsEdited_ = true;
    const std::string idStr = ruleId.toStdString();
    if (enabled) {
        config_.disabledRules.erase(idStr);
    } else {
        config_.disabledRules[idStr] = DisabledRuleConfig{
            idStr,
            justification.trimmed().isEmpty()
                ? "Disabled in RepoDoctor Desktop Settings"
                : justification.trimmed().toStdString()};
    }
    rebuildRulesCatalog();
    emit settingsChanged();
}

bool BackendController::saveConfigurationToRepo() {
    if (isScanning_ || !lastResult_.scanCompleted) {
        setStatusMessage(QStringLiteral("Scan a local repository first to save .repodoctor.json."));
        return false;
    }
    const std::filesystem::path cfgPath =
        std::filesystem::path(lastResult_.repositoryPath) / ".repodoctor.json";
    ConfigurationManager mgr;
    std::string err;
    const bool ok = mgr.saveToFile(config_, cfgPath, &err);
    setStatusMessage(
        ok ? (QStringLiteral("Saved configuration to ") +
              QString::fromStdString(cfgPath.generic_string()))
           : (QStringLiteral("Failed to save configuration: ") + QString::fromStdString(err)));
    return ok;
}

void BackendController::setStatusMessage(const QString& msg) {
    if (statusMessage_ != msg) {
        statusMessage_ = msg;
        emit statusMessageChanged();
    }
}

}  // namespace repodoctor::gui
