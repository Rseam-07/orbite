#pragma once

#include "common.h"

#include <QDateTime>
#include <QJsonArray>
#include <QMap>
#include <QSqlDatabase>

namespace orbite {

struct Project {
    QString id;
    QString root;
};

struct Draft {
    QString text;
    QStringList files;
    bool empty() const { return text.trimmed().isEmpty() && files.isEmpty(); }
};

struct Message {
    QString role;
    QString text;
    QStringList attachments;
    QJsonArray tools;
};

struct Thread {
    QString id;
    QString project;
    QString title = "新会话";
    QString remote;
    bool isolated = false;
    bool archived = false;
    bool pinned = false;
    bool useCua = false;
    bool interrupted = false;
    QDateTime updated = QDateTime::currentDateTimeUtc();
    Draft draft;
    Draft queued;
    QList<Message> messages;
    QJsonArray plan;
    QJsonObject diffs;
};

class Profile {
public:
    explicit Profile(const QString &directory);
    ~Profile();
    Profile(const Profile &) = delete;
    Profile &operator=(const Profile &) = delete;
    const QString &directory() const { return directory_; }

    QMap<QString, Project> projects();
    QMap<QString, Thread> threads();
    void save(const Project &project);
    void save(const Thread &thread);
    QString setting(const QString &key);
    void setSetting(const QString &key, const QString &value);
    int importOrbit07(const QByteArray &json);

private:
    QString directory_;
    QString databaseName_;
    QSqlDatabase db_;
};

}
