// Included in the test translation unit to reuse its temporary repository fixtures.
namespace repodoctor::testing {
namespace {
void realGitInit(const std::filesystem::path& root) {
    (void)detail::readGit(root, {"init"});
    (void)detail::readGit(root, {"config", "user.name", "RepoDoctor Tests"});
    (void)detail::readGit(root, {"config", "user.email", "tests@example.invalid"});
    (void)detail::readGit(root, {"config", "commit.gpgsign", "false"});
}
}

TEST(FingerprintRegressionTest, Sha256MatchesPublishedVectors) {
    EXPECT_EQ(detail::sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(detail::sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(detail::sha256(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(StagedContentRegressionTest, PartialStagingAndMissingWorkingFileStillScanTheIndex) {
    ScopedTempRepo repo;
    realGitInit(repo.path());
    const std::string file = "src/çağrı ü.txt";
    const auto key = makeSyntheticAwsKey();
    repo.writeFile(file, "cloud=" + key + "\n<<<<<<< HEAD\na\n=======\nb\n>>>>>>> branch\n");
    (void)detail::readGit(repo.path(), {"add", "--", file});
    repo.writeFile(file, "clean working tree\n");
    ScanConfiguration config; config.stagedOnly = true;
    LocalRepositorySource source(repo.path());
    const auto scan = ScanEngine{}.scan(source, config);
    ASSERT_TRUE(scan.scanCompleted) << scan.errorMessage;
    ASSERT_NE(findEval(scan, "RD-SEC-001"), nullptr);
    EXPECT_EQ(findEval(scan, "RD-SEC-001")->status, CheckStatus::FAIL);
    EXPECT_EQ(findEval(scan, "RD-GIT-006")->status, CheckStatus::FAIL);
    std::filesystem::remove(repo.path() / utf8ToPath(file));
    const auto missing = ScanEngine{}.scan(source, config);
    ASSERT_TRUE(missing.scanCompleted) << missing.errorMessage;
    EXPECT_EQ(missing.statistics.totalFilesDiscovered, 1U);
    EXPECT_EQ(findEval(missing, "RD-SEC-001")->status, CheckStatus::FAIL);
}

TEST(StagedContentRegressionTest, UnstagedSecretCannotContaminateAStagedCleanFile) {
    ScopedTempRepo repo; realGitInit(repo.path());
    repo.writeFile("config.txt", "clean\n");
    (void)detail::readGit(repo.path(), {"add", "--", "config.txt"});
    repo.writeFile("config.txt", "cloud=" + makeSyntheticAwsKey());
    ScanConfiguration config; config.stagedOnly = true;
    LocalRepositorySource source(repo.path());
    const auto scan = ScanEngine{}.scan(source, config);
    ASSERT_TRUE(scan.scanCompleted) << scan.errorMessage;
    EXPECT_EQ(findEval(scan, "RD-SEC-001")->status, CheckStatus::PASS);
}

TEST(IncrementalFailureRegressionTest, InvalidRevisionAndNonRepositoryFailClosed) {
    ScopedTempRepo repo; realGitInit(repo.path());
    LocalRepositorySource source(repo.path());
    ScanConfiguration config; config.sinceGitRef = "missing-revision";
    const auto scan = ScanEngine{}.scan(source, config);
    EXPECT_FALSE(scan.scanCompleted);
    EXPECT_FALSE(scan.errorMessage.empty());
    ScopedTempRepo plain; LocalRepositorySource plainSource(plain.path());
    config.sinceGitRef.clear(); config.stagedOnly = true;
    EXPECT_FALSE(ScanEngine{}.scan(plainSource, config).scanCompleted);
}

TEST(IncrementalFailureRegressionTest, EmptyStagedSetIsValidAndConflictingModesAreRejected) {
    ScopedTempRepo repo; realGitInit(repo.path());
    LocalRepositorySource source(repo.path()); ScanConfiguration config; config.stagedOnly = true;
    const auto scan = ScanEngine{}.scan(source, config);
    ASSERT_TRUE(scan.scanCompleted) << scan.errorMessage;
    EXPECT_EQ(scan.statistics.totalFilesDiscovered, 0U);
    config.sinceGitRef = "HEAD";
    EXPECT_FALSE(ScanEngine{}.scan(source, config).scanCompleted);
    config.sinceGitRef.clear(); config.scanHistory = true;
    EXPECT_FALSE(ScanEngine{}.scan(source, config).scanCompleted);
}

TEST(GitProcessSecurityTest, QuotedRepositoryPathsDoNotEnterAShell) {
    ScopedTempRepo repo;
    const auto root = repo.path() / "quote ' & dollar $(unused)";
    std::filesystem::create_directory(root); realGitInit(root);
    EXPECT_EQ(GitRepositoryInspector{}.getStagedFiles(root).size(), 0U);
    EXPECT_THROW((void)detail::readGit(root, {"rev-parse", "--git-dir"}, 1), std::runtime_error);
    std::atomic<bool> cancelled{true};
    EXPECT_THROW((void)detail::readGit(root, {"status"}, 1024, &cancelled), std::runtime_error);
}

TEST(BaselineSecurityTest, ChangedCredentialWithSameMaskedPreviewIsNotSuppressed) {
    ScopedTempRepo repo; repo.populateHealthyBaseline();
    const auto original = makeSyntheticAwsKey(); auto changed = original; changed[8] = 'Q';
    ASSERT_EQ(SecretDetectionEngine::redactSecretValue(original), SecretDetectionEngine::redactSecretValue(changed));
    repo.writeFile("legacy.txt", "cloud=" + original + "\n");
    LocalRepositorySource source(repo.path()); ScanConfiguration config;
    config.writeBaselineFilePath = pathToUtf8(repo.path() / "baseline.json");
    const auto first = ScanEngine{}.scan(source, config); ASSERT_TRUE(first.scanCompleted) << first.errorMessage;
    config.baselineFilePath = config.writeBaselineFilePath; config.writeBaselineFilePath.clear();
    const auto accepted = ScanEngine{}.scan(source, config); ASSERT_TRUE(accepted.scanCompleted);
    EXPECT_EQ(findEval(accepted, "RD-SEC-001")->status, CheckStatus::NOT_VERIFIED);
    repo.writeFile("legacy.txt", "cloud=" + changed + "\n");
    const auto scan = ScanEngine{}.scan(source, config); ASSERT_TRUE(scan.scanCompleted) << scan.errorMessage;
    EXPECT_EQ(findEval(scan, "RD-SEC-001")->status, CheckStatus::FAIL);
    EXPECT_EQ(scan.statistics.suppressedByBaseline, 0U);
    const auto json = ReportGenerator{}.generateJsonReport(scan);
    EXPECT_EQ(json.find(changed), std::string::npos);
    EXPECT_EQ(json.find(detail::sha256("cloud=" + changed)), std::string::npos);
}

TEST(BaselineSecurityTest, SameCredentialAtANewLocationIsNotSuppressed) {
    ScopedTempRepo repo; repo.populateHealthyBaseline();
    const auto line = "cloud=" + makeSyntheticAwsKey() + "\n"; repo.writeFile("legacy.txt", line);
    LocalRepositorySource source(repo.path()); ScanConfiguration config;
    config.writeBaselineFilePath = pathToUtf8(repo.path() / "baseline.json");
    ASSERT_TRUE(ScanEngine{}.scan(source, config).scanCompleted);
    config.baselineFilePath = config.writeBaselineFilePath; config.writeBaselineFilePath.clear();
    repo.writeFile("legacy.txt", line + line);
    const auto scan = ScanEngine{}.scan(source, config); ASSERT_TRUE(scan.scanCompleted);
    EXPECT_EQ(scan.statistics.suppressedByBaseline, 1U);
    EXPECT_EQ(findEval(scan, "RD-SEC-001")->status, CheckStatus::FAIL);
}

TEST(BaselineSecurityTest, InvalidMissingAndExistingOutputFilesAreRejected) {
    ScopedTempRepo repo; LocalRepositorySource source(repo.path()); ScanConfiguration config;
    config.baselineFilePath = pathToUtf8(repo.path() / "missing.json");
    EXPECT_FALSE(ScanEngine{}.scan(source, config).scanCompleted);
    repo.writeFile("invalid.json", "{\"fingerprints\":[]}");
    config.baselineFilePath = pathToUtf8(repo.path() / "invalid.json");
    EXPECT_FALSE(ScanEngine{}.scan(source, config).scanCompleted);
    config.writeBaselineFilePath = config.baselineFilePath;
    EXPECT_FALSE(ScanEngine{}.scan(source, config).scanCompleted);
    config.baselineFilePath.clear();
    EXPECT_FALSE(ScanEngine{}.scan(source, config).scanCompleted);
    std::ifstream in(repo.path() / "invalid.json"); std::string content; std::getline(in, content);
    EXPECT_EQ(content, "{\"fingerprints\":[]}");
}

TEST(HistorySecurityTest, CommitSecretsAreWithheldAndHistoricalLinesRetainTheirNumbers) {
    ScopedTempRepo repo; realGitInit(repo.path());
    const auto key = makeSyntheticAwsKey();
    repo.writeFile("past.txt", "one\ntwo\nthree\ncloud=" + key + "\n");
    (void)detail::readGit(repo.path(), {"add", "."});
    (void)detail::readGit(repo.path(), {"commit", "--no-gpg-sign", "-m", "cloud=" + key});
    repo.writeFile("past.txt", "clean\n");
    (void)detail::readGit(repo.path(), {"add", "."});
    (void)detail::readGit(repo.path(), {"commit", "--no-gpg-sign", "-m", "remove"});
    ScanConfiguration config; config.scanHistory = true; LocalRepositorySource source(repo.path());
    const auto scan = ScanEngine{}.scan(source, config); ASSERT_TRUE(scan.scanCompleted) << scan.errorMessage;
    bool found = false;
    for (const auto& f : scan.findings) for (const auto& e : f.evidence) if (!e.commitHash.empty() && f.ruleId == "RD-SEC-001") {
        found = true; EXPECT_EQ(e.lineNumber, 4U); EXPECT_EQ(e.commitMessage, "[WITHHELD]");
    }
    EXPECT_TRUE(found);
    ReportGenerator gen;
    for (const auto& report : {gen.generateConsoleReport(scan), gen.generateJsonReport(scan), gen.generateHtmlReport(scan), gen.generateSarifReport(scan)})
        EXPECT_EQ(report.find(key), std::string::npos);
}

TEST(HistorySecurityTest, ShallowHistoryIsReportedAsIncomplete) {
    ScopedTempRepo repo; realGitInit(repo.path());
    repo.writeFile("one.txt", "one"); (void)detail::readGit(repo.path(), {"add", "."});
    (void)detail::readGit(repo.path(), {"commit", "--no-gpg-sign", "-m", "initial"});
    const auto hash = detail::readGit(repo.path(), {"rev-parse", "HEAD"}); repo.writeFile(".git/shallow", hash);
    LocalRepositorySource source(repo.path()); ScanConfiguration config; config.scanHistory = true;
    const auto scan = ScanEngine{}.scan(source, config); EXPECT_FALSE(scan.scanCompleted);
    EXPECT_NE(scan.errorMessage.find("shallow"), std::string::npos);
}

TEST(ConfigurationRegressionTest, NewScanOptionsRoundTripWithoutEnablingFileWrites) {
    ScopedTempRepo repo; ScanConfiguration config; config.scanHistory = true;
    config.maxHistoryCommits = 73; config.stagedOnly = true; config.sinceGitRef = "HEAD~2";
    config.writeBaselineFilePath = "must-not-be-loaded.json";
    ConfigurationManager manager; ASSERT_TRUE(manager.saveToFile(config, repo.path() / ".repodoctor.json"));
    const auto loaded = manager.loadFromRepository(repo.path());
    EXPECT_TRUE(loaded.scanHistory); EXPECT_TRUE(loaded.stagedOnly);
    EXPECT_EQ(loaded.maxHistoryCommits, 73U); EXPECT_EQ(loaded.sinceGitRef, "HEAD~2");
    EXPECT_TRUE(loaded.writeBaselineFilePath.empty());
}

TEST(InlineSuppressionSecurityTest, QuotedMarkersCannotHideCredentials) {
    const auto content = "cloud=" + makeSyntheticAwsKey() + "; note=\"// repodoctor-ignore: RD-SEC-001\"";
    EXPECT_FALSE(SecretDetectionEngine::hasInlineSuppression(content, "RD-SEC-001"));
    EXPECT_FALSE(SecretDetectionEngine{}.scanFileContent("config.txt", content).empty());
    EXPECT_TRUE(SecretDetectionEngine::hasInlineSuppression("const char* x = \"text\"; // repodoctor-ignore: RD-SEC-001", "RD-SEC-001"));
}

TEST(ReportRedactionRegressionTest, NonSecurityEvidenceCannotLeakAnIgnoredCredential) {
    ScopedTempRepo repo; repo.populateHealthyBaseline(); const auto key = makeSyntheticAwsKey();
    repo.writeFile(".github/workflows/leak.yml", "name: leak\non: push\njobs:\n  test:\n    steps:\n"
        "      - uses: some/action@main # cloud=" + key + " repodoctor-ignore: RD-SEC-001\n");
    LocalRepositorySource source(repo.path()); const auto scan = ScanEngine{}.scan(source);
    ASSERT_TRUE(scan.scanCompleted); ASSERT_FALSE(scan.findings.empty());
    ReportGenerator gen;
    for (const auto& report : {gen.generateConsoleReport(scan), gen.generateJsonReport(scan), gen.generateHtmlReport(scan), gen.generateSarifReport(scan)})
        EXPECT_EQ(report.find(key), std::string::npos);
}

TEST(InlineSuppressionSecurityTest, PreviousLineSuppressesOneCompleteConflictBlock) {
    ScopedTempRepo repo; repo.populateHealthyBaseline();
    repo.writeFile("conflict.txt", "// repodoctor-ignore: RD-GIT-006\n<<<<<<< HEAD\na\n=======\nb\n>>>>>>> feature\n");
    LocalRepositorySource source(repo.path()); const auto scan = ScanEngine{}.scan(source);
    ASSERT_TRUE(scan.scanCompleted);
    EXPECT_EQ(findEval(scan, "RD-GIT-006")->status, CheckStatus::PASS);
    EXPECT_EQ(scan.statistics.suppressedByComment, 1U);
}

TEST(HistorySecurityTest, DeletedBinarySizeComesFromBlobMetadata) {
    ScopedTempRepo repo; realGitInit(repo.path());
    repo.writeFile("asset.bin", std::string(8192, '\0'));
    (void)detail::readGit(repo.path(), {"add", "."});
    (void)detail::readGit(repo.path(), {"commit", "--no-gpg-sign", "-m", "binary"});
    (void)detail::readGit(repo.path(), {"rm", "asset.bin"});
    (void)detail::readGit(repo.path(), {"commit", "--no-gpg-sign", "-m", "remove binary"});
    ScanConfiguration config; config.scanHistory = true; config.maxFileSizeBytes = 1024; config.largeFileThresholdBytes = 4096;
    LocalRepositorySource source(repo.path()); const auto scan = ScanEngine{}.scan(source, config);
    ASSERT_TRUE(scan.scanCompleted) << scan.errorMessage;
    const auto* eval = findEval(scan, "RD-GIT-003"); ASSERT_NE(eval, nullptr);
    EXPECT_EQ(eval->status, CheckStatus::WARNING);
    ASSERT_FALSE(eval->findings.empty()); EXPECT_FALSE(eval->findings[0].evidence[0].commitHash.empty());
}
} // namespace repodoctor::testing
