#include "rpc.h"

#include <QJsonDocument>
#include <QTimer>

namespace orbite {

RpcProcess::RpcProcess(QObject *parent, qsizetype frameLimit)
    : QObject(parent), frameLimit_(frameLimit)
{
    connect(&process_, &QProcess::started, this, &RpcProcess::started);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &RpcProcess::receive);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        process_.readAllStandardError();
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::Crashed)
            close(error == QProcess::FailedToStart ? "无法启动协议进程。" : "协议进程的输入或输出中断。");
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        receive();
        close(QString("协议进程已断开（%1）。").arg(code));
    });
}

RpcProcess::~RpcProcess()
{
    disconnect(&process_, nullptr, this, nullptr);
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1000);
    }
}

void RpcProcess::start(const QString &program, const QStringList &args, const QString &cwd)
{
    closed_ = false;
    process_.setWorkingDirectory(cwd);
    process_.start(program, args, QIODevice::ReadWrite);
}

QString RpcProcess::writeMessage(QJsonObject object)
{
    if (!connected())
        return "引擎未连接。";
    object.insert("jsonrpc", "2.0");
    auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (data.size() > frameLimit_)
        return "发送内容超过协议消息大小限制。";
    data += '\n';
    if (process_.write(data) != data.size())
        return "协议进程无法接收消息。";
    return {};
}

void RpcProcess::request(const QString &method, const QJsonObject &params, Reply reply, int timeoutMs)
{
    const auto id = ++sequence_;
    Pending pending{std::move(reply), nullptr};
    if (timeoutMs > 0) {
        pending.timer = new QTimer(this);
        pending.timer->setSingleShot(true);
        connect(pending.timer, &QTimer::timeout, this, [this, id, method] {
            complete(id, {}, {method + " 请求超时。"});
        });
        pending.timer->start(timeoutMs);
    }
    pending_.insert(id, std::move(pending));
    const auto error = writeMessage({{"id", id}, {"method", method}, {"params", params}});
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, id, error] {
            complete(id, {}, {error, 0, closed_});
        });
    }
}

void RpcProcess::notify(const QString &method, const QJsonObject &params)
{
    const auto error = writeMessage({{"method", method}, {"params", params}});
    if (!error.isEmpty())
        close(error);
}

void RpcProcess::reply(const QJsonValue &id, const QJsonObject &result)
{
    const auto error = writeMessage({{"id", id}, {"result", result}});
    if (!error.isEmpty())
        close(error);
}

void RpcProcess::reject(const QJsonValue &id)
{
    const auto error = writeMessage({{"id", id}, {"error", QJsonObject{{"code", -32601}, {"message", "Method not supported by Orbite"}}}});
    if (!error.isEmpty())
        close(error);
}

void RpcProcess::receive()
{
    const auto buffer = process_.readAllStandardOutput();
    for (qsizetype offset = 0; offset < buffer.size() && !closed_;) {
        const auto newline = buffer.indexOf('\n', offset);
        const auto length = newline < 0 ? buffer.size() - offset : newline - offset;
        if (tail_.size() + length > frameLimit_) {
            close("协议进程返回了过大的消息。");
            return;
        }
        tail_.append(buffer.constData() + offset, length);
        if (newline < 0)
            return;
        offset = newline + 1;
        auto line = std::move(tail_).trimmed();
        tail_.clear();
        if (line.isEmpty())
            continue;
        const auto doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            close("协议进程返回了无法读取的消息。");
            return;
        }
        const auto object = doc.object();
        if (object.value("method").isString()) {
            emit message(object);
            continue;
        }
        const auto id = object.value("id").toInteger(-1);
        if (!pending_.contains(id))
            continue;
        if (object.value("error").isObject()) {
            const auto error = object.value("error").toObject();
            complete(id, {}, {error.value("message").toString("引擎请求失败。"), error.value("code").toInt()});
        } else if (object.contains("result")) {
            complete(id, object.value("result"), {});
        } else {
            complete(id, {}, {"引擎没有返回请求结果。"});
        }
    }
}

void RpcProcess::complete(qint64 id, const QJsonValue &result, const RpcError &error)
{
    if (!pending_.contains(id))
        return;
    auto pending = pending_.take(id);
    if (pending.timer) {
        pending.timer->stop();
        pending.timer->deleteLater();
    }
    pending.reply(result, error);
}

void RpcProcess::close(const QString &reason)
{
    if (closed_)
        return;
    closed_ = true;
    if (process_.state() != QProcess::NotRunning) {
        process_.closeWriteChannel();
        process_.terminate();
        QTimer::singleShot(1200, this, [this] {
            if (process_.state() != QProcess::NotRunning)
                process_.kill();
        });
    }
    // Mark the connection closed before completing anything. In-flight callers should
    // treat interruption as uncertain rather than retrying a request that may have run.
    const auto keys = pending_.keys();
    for (const auto id : keys)
        complete(id, {}, {reason, 0, true});
    emit disconnected(reason);
}

}
