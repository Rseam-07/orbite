#pragma once

#include "acp.h"
#include "attachments.h"
#include "profile.h"
#include "workspace.h"

#include <QPointer>
#include <memory>

namespace orbite {

enum class Phase { Offline, Connecting, NeedAuth, Ready, Preparing, Running };
enum class SendSource { None, Composer, Queue };

struct Runtime {
    QPointer<AcpClient> client;
    Scope scope;
    Phase phase = Phase::Offline;
    QString status;
    bool authAvailable = false;
    bool cancelPending = false;
    std::shared_ptr<std::atomic_bool> cancelStaging;
    bool autoRunQueued = false;
    bool queueInFlight = false;
    SendSource source = SendSource::None;
    Draft outbound;
    QString steering;
    int replyIndex = -1;

    bool busy() const { return phase == Phase::Connecting || phase == Phase::Preparing || phase == Phase::Running; }
};

class Controller : public QObject {
    Q_OBJECT
public:
    explicit Controller(Profile &profile, QString executableOverride = {}, QObject *parent = nullptr);
    ~Controller() override;

    const QMap<QString, Project> &projects() const { return projects_; }
    const QMap<QString, Thread> &threads() const { return threads_; }
    const Thread *thread(const QString &id) const;
    Runtime runtime(const QString &id) const { return runtime_.value(id); }
    bool busy() const;
    bool applying() const { return !mutationTarget_.isEmpty(); }
    QString profileDirectory() const { return profile_.directory(); }
    QString setting(const QString &key);
    void setSetting(const QString &key, const QString &value);
    QString executable();
    QString driver();
    QString openProject(const QString &directory);
    QString newThread(const QString &project, bool isolated = false);
    void rename(const QString &id, const QString &title);
    void setPinned(const QString &id, bool pinned);
    void setArchived(const QString &id, bool archived);
    void setIsolated(const QString &id, bool isolated);
    void setCua(const QString &id, bool use);
    void setDraft(const QString &id, const Draft &draft);
    void queueDraft(const QString &id);
    void restoreQueue(const QString &id);
    void flush();

    void connectThread(const QString &id);
    void authenticate(const QString &id);
    void send(const QString &id, SendSource source = SendSource::Composer);
    void steer(const QString &id);
    void stop(const QString &id);
    void decide(const QString &id, const QString &key, bool allow);
    void configure(const QString &id, const QString &option, const QString &value);
    void listRemote(const QString &projectId);
    void authenticateRemote();
    void cancelRemote();
    int importRemote(const QString &projectId, const QList<RemoteThread> &sessions);
    int importLegacy(const QByteArray &json);
    void stopAll();

    Scope scopeFor(const QString &id, bool requireIdle = false);
    void deliver(const QString &id, const Patch &patch);
    bool driverCheckAllowed() const;
    void setDriverTesting(bool active) { driverTesting_ = active; }

signals:
    void navigationChanged();
    void threadChanged(const QString &id);
    void transcriptChanged(const QString &id);
    void statusMessage(const QString &message);
    void remoteSessions(const QString &project, const QList<RemoteThread> &sessions);
    void remoteAuthentication(bool available);
    void remoteFailed(const QString &message);

private:
    QString projectRoot(const QString &project);
    void attachClient(const QString &id, const Scope &scope, const QString &program, const QString &driver);
    void beginSend(const QString &id);
    void abortConnection(const QString &id, const QString &error);
    void disconnected(const QString &id, const QString &error);
    void closeClient(const QString &id);
    void endTurn(const QString &id, const QString &reason, const QString &error);
    void resetPending(Runtime &run);
    void saveLater(const QString &id);
    bool saveNow(const QString &id);
    bool conflicting(const QString &id, const QString &path) const;

    Profile &profile_;
    QString executableOverride_;
    QMap<QString, Project> projects_;
    QMap<QString, Thread> threads_;
    QHash<QString, Runtime> runtime_;
    QSet<QString> dirty_;
    QTimer saveTimer_;
    QPointer<AcpClient> historyClient_;
    QString mutationTarget_;
    bool driverTesting_ = false;
    bool shuttingDown_ = false;
};

}
