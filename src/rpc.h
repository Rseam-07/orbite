#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <functional>

class QTimer;

namespace orbite {

struct RpcError {
    QString text;
    int code = 0;
    bool disconnected = false;
    explicit operator bool() const { return !text.isEmpty(); }
};

class RpcProcess : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonValue &, const RpcError &)>;
    explicit RpcProcess(QObject *parent = nullptr, qsizetype frameLimit = 16 * 1024 * 1024);
    ~RpcProcess() override;

    void start(const QString &program, const QStringList &args, const QString &cwd);
    void request(const QString &method, const QJsonObject &params, Reply reply, int timeoutMs = 25000);
    void notify(const QString &method, const QJsonObject &params);
    void reply(const QJsonValue &id, const QJsonObject &result);
    void reject(const QJsonValue &id);
    void close(const QString &reason);
    bool connected() const { return !closed_ && process_.state() == QProcess::Running; }

signals:
    void started();
    void message(const QJsonObject &message);
    void disconnected(const QString &reason);

private:
    struct Pending {
        Reply reply;
        QTimer *timer = nullptr;
    };
    QString writeMessage(QJsonObject message);
    void receive();
    void complete(qint64 id, const QJsonValue &result, const RpcError &error);

    QProcess process_;
    QHash<qint64, Pending> pending_;
    QByteArray tail_;
    qsizetype frameLimit_;
    qint64 sequence_ = 0;
    bool closed_ = true;
};

}
