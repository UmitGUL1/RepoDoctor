#include "backend_controller.hpp"
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include "fake_git_environment.hpp"

namespace {
bool finishScan(repodoctor::gui::BackendController& backend) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&backend, &repodoctor::gui::BackendController::scanStateChanged,
        &loop, [&] { if (!backend.isScanning()) loop.quit(); });
    timer.start(10000);
    if (backend.isScanning()) loop.exec();
    return !backend.isScanning();
}
}

TEST(GuiScanRegression, ScanReturnsBeforeWorkerFinishesAndRejectsReentry) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    repodoctor::gui::BackendController backend;
    bool resultReadyAtCompletion = false;
    QObject::connect(&backend, &repodoctor::gui::BackendController::scanStateChanged,
        [&] { if (!backend.isScanning()) resultReadyAtCompletion = backend.hasScanResult(); });
    backend.scanLocalPath(directory.path());
    EXPECT_TRUE(backend.isScanning());
    backend.scanLocalPath(QStringLiteral("missing-directory"));
    ASSERT_TRUE(finishScan(backend));
    EXPECT_TRUE(backend.hasScanResult());
    EXPECT_TRUE(resultReadyAtCompletion);
}

TEST(GuiScanRegression, CancellationIsDeliveredWhileWorkerRuns) {
    FakeGitEnvironment fixture;
    repodoctor::gui::BackendController backend;
    backend.scanGitHubUrl(QStringLiteral("https://github.com/offline/timeout"));
    QTimer::singleShot(50, &backend, [&] { backend.cancelScan(); });
    ASSERT_TRUE(finishScan(backend));
    EXPECT_FALSE(backend.hasScanResult());
}

TEST(GuiScanRegression, RemoteCheckoutLivesUntilControllerIsDestroyed) {
    FakeGitEnvironment fixture;
    QString checkout;
    {
        repodoctor::gui::BackendController backend;
        backend.scanGitHubUrl(QStringLiteral("https://github.com/offline/success"));
        ASSERT_TRUE(finishScan(backend));
        ASSERT_TRUE(backend.hasScanResult());
        checkout = backend.currentRepoPath();
        EXPECT_TRUE(std::filesystem::exists(repodoctor::utf8ToPath(checkout.toStdString())));
    }
    EXPECT_FALSE(std::filesystem::exists(repodoctor::utf8ToPath(checkout.toStdString())));
}

TEST(GuiScanRegression, DestructionWaitsForWorkerSafely) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    {
        repodoctor::gui::BackendController backend;
        backend.scanLocalPath(directory.path());
    }
    SUCCEED();
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
