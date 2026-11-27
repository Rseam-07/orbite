#include "theme.h"
#include "window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <optional>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Orbite");
    app.setApplicationDisplayName("Orbite");
    app.setApplicationVersion(ORBITE_VERSION);
    app.setWindowIcon(QIcon(":/assets/orbite-mark.png"));
    app.setStyle("Fusion");

    QCommandLineParser parser;
    parser.setApplicationDescription("Qt 6 desktop client for Antigravity");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption profile("profile", "Use a separate local profile directory.", "directory");
    parser.addOption(profile);
    QCommandLineOption verifyPackage("verify-package", "Verify the installed desktop runtime.", "report");
    verifyPackage.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(verifyPackage);
    parser.addPositionalArgument("project", "Open this project in the workbench.", "[project]");
    parser.process(app);

    std::optional<QTemporaryDir> verificationProfile;
    if (parser.isSet(verifyPackage))
        verificationProfile.emplace();
    const auto location = verificationProfile ? verificationProfile->path() :
                          parser.isSet(profile) ? QDir(parser.value(profile)).absolutePath() :
                          QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    try {
        if (location.isEmpty() || !QDir().mkpath(location))
            throw orbite::Failure("无法创建本地资料目录。");
        QLockFile lock(location + "/workspace.lock");
        if (!lock.tryLock(0))
            throw orbite::Failure("此资料库已被运行中的 Orbite 使用。");
        orbite::Profile data(location);
        orbite::applyTheme(data.setting("theme"));
        orbite::Controller controller(data);
        orbite::Window window(controller);
        if (!parser.positionalArguments().isEmpty())
            window.openPath(QDir(parser.positionalArguments().first()).absolutePath());
        window.show();
        if (verificationProfile) {
            data.setSetting("verification", app.applicationVersion());
            QTimer::singleShot(0, &window, [&] {
                const auto report = QJsonDocument(QJsonObject{
                    {"version", app.applicationVersion()}, {"platform", app.platformName()},
                    {"architecture", QSysInfo::buildCpuArchitecture()},
                    {"databaseWritable", data.setting("verification") == app.applicationVersion()},
                    {"windowVisible", window.windowHandle() && window.isVisible()}
                }).toJson();
                QSaveFile file(parser.value(verifyPackage));
                const auto written = file.open(QIODevice::WriteOnly) && file.write(report) == report.size() &&
                                     file.commit();
                app.exit(written ? 0 : 1);
            });
        }
        const auto result = app.exec();
        controller.stopAll();
        return result;
    } catch (const orbite::Failure &error) {
        if (verificationProfile) {
            qCritical("%s", qPrintable(error.text));
            return 1;
        }
        QMessageBox message(QMessageBox::Critical, "Orbite", error.text, QMessageBox::Close);
        message.setTextFormat(Qt::PlainText);
        message.exec();
        return 1;
    }
}
