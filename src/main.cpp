#include "theme.h"
#include "window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

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
    parser.addPositionalArgument("project", "Open this project in the workbench.", "[project]");
    parser.process(app);

    const auto location = parser.isSet(profile) ? QDir(parser.value(profile)).absolutePath() :
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
        const auto result = app.exec();
        controller.stopAll();
        return result;
    } catch (const orbite::Failure &error) {
        QMessageBox message(QMessageBox::Critical, "Orbite", error.text, QMessageBox::Close);
        message.setTextFormat(Qt::PlainText);
        message.exec();
        return 1;
    }
}
