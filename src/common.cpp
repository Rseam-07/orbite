#include "common.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

namespace orbite {

CommandResult runCommand(const QString &program, const QStringList &arguments, const QString &cwd,
                         int timeoutMs, qsizetype limit, const QByteArray &input,
                         bool cleanGitEnvironment, const QString &gitIndex)
{
    QProcess process;
    process.setWorkingDirectory(cwd);
    auto env = QProcessEnvironment::systemEnvironment();
    if (cleanGitEnvironment) {
        for (const auto &key : env.keys()) {
            if (key.startsWith("GIT_"))
                env.remove(key);
        }
        env.insert("GIT_TERMINAL_PROMPT", "0");
        env.insert("GIT_OPTIONAL_LOCKS", "0");
        if (!gitIndex.isEmpty())
            env.insert("GIT_INDEX_FILE", gitIndex);
    }
    process.setProcessEnvironment(env);
    QElapsedTimer clock;
    clock.start();
    process.start(program, arguments, QIODevice::ReadWrite);
    if (!process.waitForStarted(qMin(timeoutMs, 5000)))
        throw Failure(QString("无法启动 %1。").arg(QFileInfo(program).fileName()));

    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();

    QByteArray output, error;
    do {
        if (clock.elapsed() >= timeoutMs) {
            process.kill();
            process.waitForFinished(1000);
            throw Failure(QString("%1 操作超时。").arg(QFileInfo(program).fileName()));
        }
        process.waitForReadyRead(40);
        output += process.readAllStandardOutput();
        error += process.readAllStandardError();
        if (output.size() > limit || error.size() > limit) {
            process.kill();
            process.waitForFinished(1000);
            throw Failure("命令输出超过大小限制。");
        }
    } while (process.state() != QProcess::NotRunning);

    if (process.exitStatus() != QProcess::NormalExit)
        throw Failure(QString("%1 意外结束。").arg(QFileInfo(program).fileName()));
    return {process.exitCode(), output, error};
}

QString findTool(const QString &name, const QString &preferred)
{
    if (!preferred.isEmpty()) {
        const QFileInfo file(preferred);
        return file.isAbsolute() && file.isFile() && file.isExecutable() ? file.canonicalFilePath() : QString();
    }
    const auto executable = QStandardPaths::findExecutable(name);
    if (!executable.isEmpty())
        return executable;

    const QString home = QDir::homePath();
    for (const auto &dir : QStringList{home + "/.local/bin", home + "/.antigravity/bin",
                                       "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"}) {
        const QFileInfo file(dir + "/" + name);
        if (file.isFile() && file.isExecutable())
            return file.canonicalFilePath();
    }
#ifdef Q_OS_MACOS
    if (name == "cua-driver") {
        for (const auto &dir : QStringList{"/Applications", home + "/Applications"}) {
            const QFileInfo file(dir + "/CuaDriver.app/Contents/MacOS/cua-driver");
            if (file.isExecutable())
                return file.canonicalFilePath();
        }
    }
#endif
    return {};
}

QString canonicalDirectory(const QString &path)
{
    const QFileInfo info(path);
    return info.isAbsolute() && info.isDir() ? info.canonicalFilePath() : QString();
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool validId(const QString &id)
{
    static const QRegularExpression pattern("^[a-z0-9-]{1,70}$");
    return pattern.match(id).hasMatch();
}

QString jsonText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson()).trimmed();
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson()).trimmed();
    return {};
}

}
