#include "backend_controller.hpp"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <iostream>

int main(int argc, char* argv[]) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RepoDoctor"));
    QApplication::setOrganizationName(QStringLiteral("RepoDoctor"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QString initialScanPath;
    QString screenshotDir;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == QStringLiteral("--scan-path") && i + 1 < args.size()) {
            initialScanPath = args[++i];
        } else if (args[i] == QStringLiteral("--capture-screenshots") && i + 1 < args.size()) {
            screenshotDir = args[++i];
        }
    }

    repodoctor::gui::BackendController backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        std::cerr << "Failed to load RepoDoctor QML interface.\n";
        return 1;
    }

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (window == nullptr) {
        return 1;
    }

    const auto capturePages = [&]() {
    if (!screenshotDir.isEmpty()) {
        QDir dir(screenshotDir);
        if (!dir.exists()) {
            dir.mkpath(QStringLiteral("."));
        }

        const QStringList pageNames = {
            QStringLiteral("home"),
            QStringLiteral("dashboard"),
            QStringLiteral("findings"),
            QStringLiteral("autofix"),
            QStringLiteral("reports"),
            QStringLiteral("settings")
        };

        for (int idx = 0; idx < pageNames.size(); ++idx) {
            QTimer::singleShot(300 + idx * 350, [window, idx, pageNames, screenshotDir]() {
                window->setProperty("currentTabIndex", idx);
                QTimer::singleShot(180, [window, idx, pageNames, screenshotDir]() {
                    const QImage img = window->grabWindow();
                    const QString outFile =
                        QDir(screenshotDir).filePath(pageNames[idx] + QStringLiteral(".png"));
                    if (img.isNull() || !img.save(outFile, "PNG")) {
                        QCoreApplication::exit(2);
                        return;
                    }
                    if (idx + 1 == pageNames.size()) {
                        QCoreApplication::quit();
                    }
                });
            });
        }
    }

    };
    if (!initialScanPath.isEmpty()) {
        QObject::connect(&backend, &repodoctor::gui::BackendController::scanStateChanged,
            &app, [&]() {
                if (!backend.isScanning()) {
                    if (!backend.hasScanResult()) {
                        std::cerr << backend.statusMessage().toStdString() << "\n";
                        QCoreApplication::exit(3);
                    }
                    else capturePages();
                }
            });
        QTimer::singleShot(0, &app, [&]() { backend.scanLocalPath(initialScanPath); });
    } else {
        capturePages();
    }
    return QApplication::exec();
}
