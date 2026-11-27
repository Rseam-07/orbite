#include "acp.h"
#include "workspace.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

namespace orbite {

AcpClient::AcpClient(const QString &program, const QString &cwd, QObject *parent)
    : QObject(parent), program_(program), cwd_(cwd)
{
    stopTimer_.setSingleShot(true);
    connect(&stopTimer_, &QTimer::timeout, this, [this] {
        close("引擎未确认停止，连接已断开。请检查工作区的实际改动。");
    });
    connect(&rpc_, &RpcProcess::started, this, [this] {
        rpc_.request("initialize", {{"protocolVersion", 1}, {"clientCapabilities", QJsonObject{}},
                     {"clientInfo", QJsonObject{{"name", "orbite"}, {"title", "Orbite"}, {"version", ORBITE_VERSION}}}},
                     [this](const auto &data, const auto &error) { initialized(data, error); });
    });
    connect(&rpc_, &RpcProcess::message, this, &AcpClient::incoming);
    connect(&rpc_, &RpcProcess::disconnected, this, [this](const QString &error) {
        stopTimer_.stop();
        ready_ = active_ = steering_ = opening_ = false;
        permissions_.clear();
        permissionIds_.clear();
        emit disconnected(error);
    });
}

void AcpClient::open(const QString &remote, bool replay, const QString &driver)
{
    requested_ = remote;
    wantsReplay_ = replay;
    driver_ = driver;
    start();
}

void AcpClient::listSessions()
{
    historyOnly_ = true;
    start();
}

void AcpClient::start()
{
    rpc_.start(program_, {"acp", "--sandbox"}, cwd_);
}

void AcpClient::initialized(const QJsonValue &data, const RpcError &error)
{
    if (error) {
        sessionFailure(error);
        return;
    }
    const auto result = data.toObject();
    if (result.value("protocolVersion").toInt() != 1) {
        emit connectionError("引擎未协商 ACP v1。");
        return;
    }
    capabilities_ = result.value("agentCapabilities").toObject();
    const auto methods = result.value("authMethods").toArray();
    for (const auto &value : methods) {
        const auto method = value.toObject();
        if (method.value("id").toString() == "oauth-personal" && method.value("type").toString() != "terminal")
            authAvailable_ = true;
    }
    if (!historyOnly_) {
        requestSession();
        return;
    }
    if (!capabilities_.value("sessionCapabilities").toObject().value("list").isObject()) {
        emit connectionError("当前 Antigravity CLI 未提供会话列表。");
        return;
    }
    requestPage();
}

void AcpClient::sessionFailure(const RpcError &error)
{
    if (error.disconnected)
        return;
    if (error.code == -32000) {
        emit needsAuthentication(authAvailable_);
    } else {
        emit connectionError(error.text);
    }
}

void AcpClient::authenticate()
{
    if (!authAvailable_ || authenticating_)
        return;
    authenticating_ = true;
    rpc_.request("authenticate", {{"methodId", "oauth-personal"}}, [this](const auto &, const RpcError &error) {
        authenticating_ = false;
        if (error) {
            sessionFailure(error);
            return;
        }
        if (historyOnly_)
            requestPage();
        else
            requestSession();
    }, 300000);
}

void AcpClient::requestSession(bool useResume)
{
    const bool existing = !requested_.isEmpty();
    const auto sessions = capabilities_.value("sessionCapabilities").toObject();
    const bool resume = existing && useResume && !wantsReplay_ && sessions.value("resume").isObject();
    const bool canLoad = capabilities_.value("loadSession").toBool() || sessions.value("load").isObject();
    if (existing && !resume && !canLoad) {
        emit connectionError("当前 Antigravity CLI 无法恢复会话。可以新建会话，原来的对话仍在本地。");
        return;
    }
    QJsonArray mcp;
    if (!driver_.isEmpty())
        mcp.append(QJsonObject{{"name", "cua-driver"}, {"command", driver_}, {"args", QJsonArray{"mcp"}}, {"env", QJsonArray{}}});
    QJsonObject params{{"cwd", cwd_}, {"mcpServers", mcp}};
    if (existing)
        params.insert("sessionId", requested_);
    opening_ = true;
    replay_.clear();
    replayLength_ = 0;
    earlyUpdates_.clear();
    const auto method = resume ? "session/resume" : existing ? "session/load" : "session/new";
    rpc_.request(method, params, [this, resume, canLoad](const auto &data, const RpcError &error) {
        opening_ = false;
        if (resume && canLoad && error.code == -32601) {
            requestSession(false);
            return;
        }
        if (error) {
            sessionFailure(error);
            return;
        }
        const auto result = data.toObject();
        remote_ = requested_.isEmpty() ? result.value("sessionId").toString() : requested_;
        if (remote_.isEmpty()) {
            emit connectionError("Antigravity 未返回会话 ID。");
            return;
        }
        ready_ = true;
        setOptions(result);
        for (const auto &[sender, change] : std::as_const(earlyUpdates_))
            update(sender, change);
        earlyUpdates_.clear();
        emit connected(remote_, std::exchange(replay_, {}));
    }, existing ? 60000 : 25000);
}

void AcpClient::requestPage(const QString &cursor)
{
    QJsonObject params{{"cwd", cwd_}};
    if (!cursor.isEmpty()) {
        if (cursors_.size() >= 100 || cursors_.contains(cursor)) {
            emit connectionError("会话列表分页异常。");
            return;
        }
        cursors_.insert(cursor);
        params.insert("cursor", cursor);
    } else {
        remoteThreads_.clear();
        cursors_.clear();
    }
    rpc_.request("session/list", params, [this](const auto &data, const RpcError &error) {
        if (error) {
            sessionFailure(error);
            return;
        }
        const auto result = data.toObject();
        if (!result.value("sessions").isArray()) {
            emit connectionError("Antigravity 返回的会话列表格式无效。");
            return;
        }
        for (const auto &entry : result.value("sessions").toArray()) {
            const auto row = entry.toObject();
            const auto id = row.value("sessionId").toString();
            const auto cwd = row.value("cwd").toString();
            if (id.isEmpty() || (cwd != cwd_ && canonicalDirectory(cwd) != cwd_))
                continue;
            remoteThreads_.append({id, cwd_, row.value("title").toString(),
                                   QDateTime::fromString(row.value("updatedAt").toString(), Qt::ISODate)});
        }
        const auto next = result.value("nextCursor").toString();
        if (next.isEmpty())
            emit sessionsListed(remoteThreads_);
        else
            requestPage(next);
    });
}

bool AcpClient::supportsImages() const
{
    return capabilities_.value("promptCapabilities").toObject().value("image").toBool();
}

void AcpClient::prompt(const QJsonArray &content)
{
    Q_ASSERT(ready_ && !active_);
    active_ = true;
    stopping_ = steering_ = promptFinished_ = false;
    stopReason_.clear();
    promptError_.clear();
    rpc_.request("session/prompt", {{"sessionId", remote_}, {"prompt", content}}, [this](const auto &data, const RpcError &error) {
        if (error.disconnected)
            return;
        promptFinished_ = true;
        promptError_ = error.text;
        stopReason_ = stopping_ ? "cancelled" : data.toObject().value("stopReason").toString("unknown");
        completeTurnIfReady();
    }, 0);
}

void AcpClient::steer(const QString &text)
{
    if (!canSteer())
        return;
    steering_ = true;
    const QJsonArray prompt{QJsonObject{{"type", "text"}, {"text", text}}};
    rpc_.request("session/prompt", {{"sessionId", remote_}, {"prompt", prompt}}, [this, text](const auto &, const RpcError &error) {
        if (error.disconnected)
            return;
        steering_ = false;
        emit steeringEnded(text, stopping_ ? "本轮已停止，补充内容保留为草稿。" : error.text);
        completeTurnIfReady();
    }, 0);
}

void AcpClient::completeTurnIfReady()
{
    if (!promptFinished_ || steering_)
        return;
    for (const auto &key : permissions_.keys())
        resolvePermission(key, false, true);
    stopTimer_.stop();
    active_ = stopping_ = promptFinished_ = false;
    emit turnEnded(stopReason_, promptError_);
}

void AcpClient::cancel()
{
    if (!active_ || stopping_)
        return;
    stopping_ = true;
    for (const auto &key : permissions_.keys())
        resolvePermission(key, false, true);
    stopTimer_.start(8000);
    rpc_.notify("session/cancel", {{"sessionId", remote_}});
}

void AcpClient::decide(const QString &key, bool allow)
{
    if (!permissions_.contains(key))
        return;
    if (allow && permissions_.value(key).allow.isEmpty())
        return;
    resolvePermission(key, allow);
}

void AcpClient::resolvePermission(const QString &key, bool allow, bool cancel)
{
    const auto permission = permissions_.take(key);
    const auto id = permissionIds_.take(key);
    const auto choice = allow ? permission.allow : permission.deny;
    const auto outcome = cancel || choice.isEmpty() ? QJsonObject{{"outcome", "cancelled"}} :
                         QJsonObject{{"outcome", "selected"}, {"optionId", choice}};
    rpc_.reply(id, {{"outcome", outcome}});
    emit permissionsChanged();
}

void AcpClient::configure(const QString &id, const QString &value)
{
    if (!ready_ || active_)
        return;
    for (const auto &option : std::as_const(options_)) {
        if (option.id != id)
            continue;
        for (const auto &[candidate, label] : option.choices) {
            if (candidate != value)
                continue;
            rpc_.request("session/set_config_option", {{"sessionId", remote_}, {"configId", id}, {"value", value}},
                         [this](const auto &data, const RpcError &error) {
                if (!error)
                    setOptions(data.toObject());
                else if (!error.disconnected) {
                    emit optionsChanged();
                    emit notice(error.text);
                }
            });
            return;
        }
        return;
    }
}

void AcpClient::close(const QString &reason)
{
    rpc_.close(reason);
}

void AcpClient::incoming(const QJsonObject &object)
{
    const auto method = object.value("method").toString();
    const auto params = object.value("params").toObject();
    if (object.contains("id")) {
        if (method == "session/request_permission")
            permissionRequest(object.value("id"), params);
        else
            rpc_.reject(object.value("id"));
    } else if (method == "session/update") {
        update(params.value("sessionId").toString(), params.value("update").toObject());
    }
}

void AcpClient::permissionRequest(const QJsonValue &id, const QJsonObject &params)
{
    if (!active_ || params.value("sessionId").toString() != remote_) {
        rpc_.reject(id);
        return;
    }
    const auto call = params.value("toolCall").toObject();
    QStringList detail;
    for (const auto &location : call.value("locations").toArray()) {
        const auto path = location.toObject().value("path").toString();
        if (!path.isEmpty())
            detail << path;
    }
    const auto raw = jsonText(call.value("rawInput"));
    if (!raw.isEmpty())
        detail << raw;
    const auto description = detail.join('\n');
    if (stopping_ || description.isEmpty() || description.size() > 64000 || permissions_.size() >= 32) {
        rpc_.reply(id, {{"outcome", QJsonObject{{"outcome", "cancelled"}}}});
        if (!stopping_)
            emit notice("已取消无法完整显示的审批请求。");
        return;
    }
    const auto key = QString::fromUtf8(QJsonDocument(QJsonArray{id}).toJson(QJsonDocument::Compact));
    Permission permission{key, call.value("title").toString("请求执行操作").left(600),
                          description, {}, {}};
    for (const auto &choice : params.value("options").toArray()) {
        const auto option = choice.toObject();
        if (option.value("kind").toString() == "allow_once" && permission.allow.isEmpty())
            permission.allow = option.value("optionId").toString();
        if (option.value("kind").toString() == "reject_once" && permission.deny.isEmpty())
            permission.deny = option.value("optionId").toString();
    }
    permissionIds_.insert(key, id);
    permissions_.insert(key, permission);
    emit permissionsChanged();
}

void AcpClient::update(const QString &sender, const QJsonObject &change)
{
    const auto kind = change.value("sessionUpdate").toString();
    const bool config = kind == "config_option_update";
    const bool commands = kind == "available_commands_update";
    if (opening_ && (config || commands) && earlyUpdates_.size() < 50) {
        earlyUpdates_.append({sender, change});
        return;
    }
    if (opening_ && wantsReplay_ && !requested_.isEmpty() && sender == requested_ &&
        (kind == "agent_message_chunk" || kind == "user_message_chunk")) {
        const auto block = change.value("content").toObject();
        const auto text = block.value("text").toString();
        if (block.value("type").toString() != "text" || text.isEmpty() || replayLength_ + text.size() > 8 * 1024 * 1024)
            return;
        const auto role = kind == "agent_message_chunk" ? QString("assistant") : QString("user");
        if (replay_.isEmpty() || replay_.last().role != role)
            replay_.append({role, {}, {}, {}});
        replay_.last().text += text;
        replayLength_ += text.size();
        return;
    }
    if (sender != remote_ || !ready_)
        return;
    if (config) {
        setOptions(change);
        return;
    }
    if (commands) {
        static const QRegularExpression commandName("^[A-Za-z0-9_-]{1,60}$");
        commands_.clear();
        for (const auto &entry : change.value("availableCommands").toArray()) {
            const auto cmd = entry.toObject();
            const auto name = cmd.value("name").toString();
            if (commandName.match(name).hasMatch())
                commands_.append({name, cmd.value("description").toString().left(160)});
            if (commands_.size() == 100)
                break;
        }
        emit optionsChanged();
        return;
    }
    if (!active_)
        return;
    if (kind == "agent_message_chunk" && change.value("content").toObject().value("type").toString() == "text") {
        emit delta(change.value("content").toObject().value("text").toString());
    } else if (kind == "plan") {
        emit plan(change.value("entries").toArray());
    } else if (kind == "tool_call" || kind == "tool_call_update") {
        emit tool(change);
        if (change.value("status").toString() != "completed")
            return;
        for (const auto &entry : change.value("content").toArray()) {
            const auto diff = entry.toObject();
            if (diff.value("type").toString() != "diff" || !diff.value("newText").isString())
                continue;
            const auto source = diff.value("path").toString();
            const auto before = diff.value("oldText").toString();
            const auto after = diff.value("newText").toString();
            const auto relative = QDir(cwd_).relativeFilePath(QDir::cleanPath(source));
            if (QDir::isAbsolutePath(source) && workspaceRelativePath(relative) &&
                before.size() <= 80000 && after.size() <= 80000)
                emit fileChange(relative, before, after);
        }
    }
}

void AcpClient::setOptions(const QJsonObject &document)
{
    if (!document.value("configOptions").isArray())
        return;
    static const QRegularExpression restricted("bypass|danger|skip.?permissions|auto.?approve|allow.?all|yolo",
                                               QRegularExpression::CaseInsensitiveOption);
    options_.clear();
    for (const auto &entry : document.value("configOptions").toArray()) {
        const auto option = entry.toObject();
        const auto category = option.value("category").toString();
        if (option.value("type").toString() != "select" ||
            (category != "model" && category != "thought_level" && category != "mode"))
            continue;
        Config config{option.value("id").toString(), category, option.value("name").toString(category),
                      option.value("currentValue").toString(), {}};
        const auto append = [&](const QJsonObject &item) {
            const auto value = item.value("value").toString();
            const auto title = item.value("name").toString(value);
            if (!value.isEmpty() && !restricted.match(value + " " + title).hasMatch())
                config.choices.append({value, title});
        };
        for (const auto &value : option.value("options").toArray()) {
            const auto item = value.toObject();
            if (item.value("options").isArray()) {
                for (const auto &groupValue : item.value("options").toArray())
                    append(groupValue.toObject());
            } else {
                append(item);
            }
        }
        if (!config.id.isEmpty() && !config.choices.isEmpty())
            options_.append(config);
    }
    emit optionsChanged();
}

}
