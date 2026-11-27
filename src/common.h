#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <exception>

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
QString findTool(const QString &name, const QString &preferred = {});
QString canonicalDirectory(const QString &path);
QString newId();
bool validId(const QString &id);
QString jsonText(const QJsonValue &value);

}
