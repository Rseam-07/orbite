#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <exception>

class QProcess;

namespace orbite {

class Failure : public std::exception {
public:
    explicit Failure(QString message) : text(std::move(message)) {}
    QString text;
};

struct CommandResult {
    int code;
    QByteArray output;
    QByteArray error;
};

CommandResult runCommand(const QString &program, const QStringList &arguments, const QString &cwd,
                         int timeoutMs, qsizetype limit, const QByteArray &input = {},
                         bool cleanGitEnvironment = false, const QString &gitIndex = {});
void configureBackgroundProcess(QProcess &process);
QString findTool(const QString &name, const QString &preferred = {});
QString canonicalDirectory(const QString &path);
bool samePath(const QString &first, const QString &second);
bool withinDirectory(const QString &root, const QString &path);
bool openTerminal(const QString &directory, const QString &toolDirectory = {});
QString newId();
bool validId(const QString &id);
QString jsonText(const QJsonValue &value);

}
