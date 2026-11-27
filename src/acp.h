#pragma once

#include "profile.h"
#include "rpc.h"

#include <QSet>
#include <QTimer>

namespace orbite {

struct Permission {
    QString key;
    QString title;
    QString detail;
    QString allow;
    QString deny;
};

struct Config {
    QString id;
    QString category;
    QString label;
    QString value;
    QList<QPair<QString, QString>> choices;
};

struct RemoteThread {
    QString id;
    QString root;
    QString title;
    QDateTime updated;
};

class AcpClient : public QObject {
    Q_OBJECT
public:
    explicit AcpClient(const QString &program, const QString &cwd, QObject *parent = nullptr);
    void open(const QString &remote, bool replay, const QString &driver = {});
    void listSessions();
    void authenticate();
    void prompt(const QJsonArray &content);
    void steer(const QString &text);
    void cancel();
    void decide(const QString &key, bool allow);
    void configure(const QString &id, const QString &value);
    void close(const QString &reason = "连接已关闭。");

    bool ready() const { return ready_; }
    bool supportsImages() const;
    bool authAvailable() const { return authAvailable_; }
    bool active() const { return active_; }
    bool canSteer() const { return active_ && !steering_ && !stopping_ && !promptFinished_; }
    const QList<Config> &options() const { return options_; }
    const QList<QPair<QString, QString>> &commands() const { return commands_; }
    const QMap<QString, Permission> &permissions() const { return permissions_; }

signals:
    void connected(const QString &remote, const QList<Message> &history);
    void connectionError(const QString &error);
    void disconnected(const QString &error);
    void needsAuthentication(bool available);
    void sessionsListed(const QList<RemoteThread> &threads);
    void optionsChanged();
    void delta(const QString &text);
    void plan(const QJsonArray &entries);
    void tool(const QJsonObject &tool);
    void fileChange(const QString &path, const QString &before, const QString &after);
    void permissionsChanged();
    void turnEnded(const QString &reason, const QString &error);
    void steeringEnded(const QString &text, const QString &error);
    void notice(const QString &text);

private:
    void start();
    void initialized(const QJsonValue &data, const RpcError &error);
    void requestSession(bool useResume = true);
    void requestPage(const QString &cursor = {});
    void incoming(const QJsonObject &object);
    void permissionRequest(const QJsonValue &id, const QJsonObject &params);
    void update(const QString &remote, const QJsonObject &change);
    void setOptions(const QJsonObject &document);
    void resolvePermission(const QString &key, bool allow, bool cancel = false);
    void completeTurnIfReady();
    void sessionFailure(const RpcError &error);

    RpcProcess rpc_;
    QTimer stopTimer_;
    QString program_;
    QString cwd_;
    QString requested_;
    QString remote_;
    QString driver_;
    QJsonObject capabilities_;
    QList<Config> options_;
    QList<QPair<QString, QString>> commands_;
    QList<Message> replay_;
    QList<QPair<QString, QJsonObject>> earlyUpdates_;
    QMap<QString, Permission> permissions_;
    QMap<QString, QJsonValue> permissionIds_;
    QList<RemoteThread> remoteThreads_;
    QSet<QString> cursors_;
    qsizetype replayLength_ = 0;
    QString stopReason_;
    QString promptError_;
    bool authAvailable_ = false;
    bool authenticating_ = false;
    bool historyOnly_ = false;
    bool wantsReplay_ = false;
    bool opening_ = false;
    bool ready_ = false;
    bool active_ = false;
    bool steering_ = false;
    bool stopping_ = false;
    bool promptFinished_ = false;
};

}
