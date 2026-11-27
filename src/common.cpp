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

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace orbite {

void configureBackgroundProcess(QProcess &process)
{
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#else
    Q_UNUSED(process);
#endif
}

CommandResult runCommand(const QString &program, const QStringList &arguments, const QString &cwd,
                         int timeoutMs, qsizetype limit, const QByteArray &input,
                         bool cleanGitEnvironment, const QString &gitIndex)
{
    QProcess process;
    configureBackgroundProcess(process);
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

namespace {

QString executablePath(const QString &path)
{
    const QFileInfo file(path);
    if (!file.isAbsolute() || !file.isFile() || !file.isExecutable())
        return {};
#ifdef Q_OS_WIN
    if (file.suffix().compare("exe", Qt::CaseInsensitive) != 0)
        return {};
#endif
    return file.canonicalFilePath();
}

Qt::CaseSensitivity pathCase()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

}

QString findTool(const QString &name, const QString &preferred)
{
    if (!preferred.isEmpty())
        return executablePath(preferred);

    const QString home = QDir::homePath();
    QStringList directories;
    auto filename = name;
#ifdef Q_OS_WIN
    filename += ".exe";
    const auto local = qEnvironmentVariable("LOCALAPPDATA", home + "/AppData/Local");
    if (name == "agy" && QDir::isAbsolutePath(local))
        directories << local + "/agy/bin";
    if (name == "git") {
        for (const auto &base : {local + "/Programs", qEnvironmentVariable("ProgramW6432"),
                                 qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramFiles(x86)")}) {
            if (QDir::isAbsolutePath(base))
                directories << base + "/Git/cmd";
        }
    }
#else
    directories << home + "/.local/bin" << home + "/.antigravity/bin"
                << "/opt/homebrew/bin" << "/usr/local/bin" << "/usr/bin";
#endif
    const auto inherited = qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (auto dir : inherited) {
#ifdef Q_OS_WIN
        if (dir.startsWith('"') && dir.endsWith('"'))
            dir = dir.mid(1, dir.size() - 2);
#endif
        if (QDir::isAbsolutePath(dir)) {
            const auto path = executablePath(QDir(dir).filePath(filename));
            if (!path.isEmpty())
                return path;
        }
    }
    for (const auto &dir : directories) {
        const auto path = executablePath(QDir(dir).filePath(filename));
        if (!path.isEmpty())
            return path;
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

bool samePath(const QString &first, const QString &second)
{
    return QString::compare(QDir::fromNativeSeparators(first), QDir::fromNativeSeparators(second), pathCase()) == 0;
}

bool withinDirectory(const QString &root, const QString &path)
{
    if (root.isEmpty() || path.isEmpty())
        return false;
    auto prefix = QDir::fromNativeSeparators(root);
    if (!prefix.endsWith('/'))
        prefix += '/';
    return samePath(root, path) || QDir::fromNativeSeparators(path).startsWith(prefix, pathCase());
}

bool openTerminal(const QString &directory, const QString &toolDirectory)
{
    const auto cwd = canonicalDirectory(directory);
    if (cwd.isEmpty())
        return false;
#ifdef Q_OS_MACOS
    Q_UNUSED(toolDirectory);
    return QProcess::startDetached("/usr/bin/open", {"-a", "Terminal", cwd});
#elif defined(Q_OS_WIN)
    // Follow dsh-desktop's shell ordering. Use a native console and a working
    // directory so paths need no interpolation into shell commands.
    const auto system = qEnvironmentVariable("SystemRoot");
    const QStringList shells{
        findTool("pwsh"),
        system + "/System32/WindowsPowerShell/v1.0/powershell.exe",
        system + "/System32/cmd.exe"
    };
    for (const auto &shell : shells) {
        const auto program = executablePath(shell);
        if (program.isEmpty())
            continue;
        QProcess process;
        process.setProgram(program);
        process.setWorkingDirectory(cwd);
        process.setArguments(QFileInfo(program).baseName().compare("cmd", Qt::CaseInsensitive) == 0
                             ? QStringList{"/D"} : QStringList{"-NoLogo"});
        auto environment = QProcessEnvironment::systemEnvironment();
        if (!toolDirectory.isEmpty())
            environment.insert("PATH", QDir::toNativeSeparators(toolDirectory) + ";" + environment.value("PATH"));
        process.setProcessEnvironment(environment);
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags &= ~CREATE_NO_WINDOW;
            args->flags |= CREATE_NEW_CONSOLE;
            args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
        });
        if (process.startDetached())
            return true;
    }
    return false;
#else
    Q_UNUSED(toolDirectory);
    const auto terminal = findTool("x-terminal-emulator");
    return !terminal.isEmpty() && QProcess::startDetached(terminal, {}, cwd);
#endif
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
