#include "controller.h"
#include "jobs.h"

#include <QDir>
#include <QFileInfo>

namespace orbite {
namespace {

QString titleFor(const QString &text)
{
    const auto title = text.simplified().left(62);
    return title.isEmpty() ? QString("附件任务") : title;
}

bool sameDraft(const Draft &a, const Draft &b)
{
    return a.text == b.text && a.files == b.files;
}

struct PreparedPrompt {
    AttachmentBatch batch;
    QJsonArray blocks;
};

}

Controller::Controller(Profile &profile, QString executableOverride, QObject *parent)
    : QObject(parent), profile_(profile), executableOverride_(std::move(executableOverride)),
      projects_(profile.projects()), threads_(profile.threads())
{
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(350);
    connect(&saveTimer_, &QTimer::timeout, this, &Controller::flush);
}

Controller::~Controller()
{
    stopAll();
}

const Thread *Controller::thread(const QString &id) const
{
    const auto found = threads_.constFind(id);
    return found != threads_.cend() ? &found.value() : nullptr;
}

QString Controller::setting(const QString &key)
{
    return profile_.setting(key);
}

void Controller::setSetting(const QString &key, const QString &value)
{
    profile_.setSetting(key, value);
}

QString Controller::executable()
{
    return findTool("agy", executableOverride_.isEmpty() ? setting("agy") : executableOverride_);
}

QString Controller::driver()
{
    return findTool("cua-driver", setting("driver"));
}

QString Controller::openProject(const QString &directory)
{
    const auto root = canonicalDirectory(directory);
    if (root.isEmpty() || QDir(root).isRoot() || samePath(root, canonicalDirectory(QDir::homePath())))
        throw Failure("请选择一个具体的项目目录。");
    for (const auto &project : std::as_const(projects_)) {
        if (samePath(project.root, root)) {
            setSetting("unconfirmedProject/" + project.id, "");
            return project.id;
        }
    }
    const Project project{newId(), root};
    profile_.save(project);
    projects_.insert(project.id, project);
    emit navigationChanged();
    return project.id;
}

QString Controller::newThread(const QString &project, bool isolated)
{
    if (!projects_.contains(project))
        throw Failure("请先打开项目。");
    Thread created;
    created.id = newId();
    created.project = project;
    created.isolated = isolated;
    profile_.save(created);
    threads_.insert(created.id, created);
    emit navigationChanged();
    return created.id;
}

void Controller::rename(const QString &id, const QString &title)
{
    if (!threads_.contains(id) || title.trimmed().isEmpty())
        return;
    threads_[id].title = title.trimmed().left(120);
    saveLater(id);
    emit navigationChanged();
    emit threadChanged(id);
}

void Controller::setPinned(const QString &id, bool pinned)
{
    if (!threads_.contains(id))
        return;
    threads_[id].pinned = pinned;
    saveLater(id);
    emit navigationChanged();
    emit threadChanged(id);
}

void Controller::setArchived(const QString &id, bool archived)
{
    if (!threads_.contains(id) || runtime_.value(id).busy())
        return;
    if (archived)
        closeClient(id);
    threads_[id].archived = archived;
    saveNow(id);
    emit navigationChanged();
    emit threadChanged(id);
}

void Controller::setIsolated(const QString &id, bool isolated)
{
    if (!threads_.contains(id) || !threads_[id].remote.isEmpty() || runtime_.value(id).busy() ||
        runtime_.value(id).client)
        return;
    threads_[id].isolated = isolated;
    runtime_[id].scope = {};
    saveNow(id);
    emit threadChanged(id);
}

void Controller::setCua(const QString &id, bool use)
{
    if (!threads_.contains(id) || threads_[id].archived || runtime_.value(id).busy())
        return;
    if (use && driver().isEmpty())
        throw Failure("请先在设置中指定已安装的 Cua Driver。");
    closeClient(id);
    threads_[id].useCua = use;
    saveNow(id);
    emit threadChanged(id);
}

void Controller::setDraft(const QString &id, const Draft &draft)
{
    if (!threads_.contains(id) || threads_[id].archived || sameDraft(threads_[id].draft, draft))
        return;
    threads_[id].draft = draft;
    saveLater(id);
    emit threadChanged(id);
}

void Controller::queueDraft(const QString &id)
{
    if (!threads_.contains(id) || runtime_.value(id).phase != Phase::Running)
        return;
    auto &thread = threads_[id];
    if (thread.draft.empty() || !thread.queued.empty())
        return;
    thread.queued = std::exchange(thread.draft, {});
    auto &run = runtime_[id];
    run.autoRunQueued = saveNow(id);
    emit threadChanged(id);
}

void Controller::restoreQueue(const QString &id)
{
    if (!threads_.contains(id) || runtime_.value(id).source == SendSource::Queue ||
        runtime_.value(id).queueInFlight)
        return;
    auto &thread = threads_[id];
    if (!thread.draft.empty())
        throw Failure("请先发送或清空编辑框，再取回排队消息。");
    runtime_[id].autoRunQueued = false;
    thread.draft = std::exchange(thread.queued, {});
    saveNow(id);
    emit threadChanged(id);
}

void Controller::saveLater(const QString &id)
{
    dirty_.insert(id);
    if (!saveTimer_.isActive())
        saveTimer_.start();
}

bool Controller::saveNow(const QString &id)
{
    try {
        profile_.save(threads_.value(id));
        dirty_.remove(id);
        return true;
    } catch (const Failure &error) {
        dirty_.insert(id);
        emit statusMessage(error.text);
        return false;
    }
}

void Controller::flush()
{
    saveTimer_.stop();
    const auto changed = dirty_;
    for (const auto &id : changed)
        saveNow(id);
}

QString Controller::projectRoot(const QString &project)
{
    if (!projects_.contains(project))
        throw Failure("请先打开项目。");
    const auto path = projects_.value(project).root;
    if (setting("unconfirmedProject/" + project) == "yes")
        throw Failure("这是从 0.7 导入的项目。首次使用前，请通过「打开项目」重新选择其目录。");
    if (path.isEmpty() || !samePath(canonicalDirectory(path), path))
        throw Failure("项目目录不可用，请重新选择。");
    return path;
}

bool Controller::busy() const
{
    if (applying() || driverTesting_)
        return true;
    for (const auto &run : runtime_) {
        if (run.busy())
            return true;
    }
    return false;
}

bool Controller::conflicting(const QString &id, const QString &path) const
{
    if (pathsOverlap(mutationTarget_, path))
        return true;
    for (auto other = runtime_.cbegin(); other != runtime_.cend(); ++other) {
        if (other.key() != id && (other->phase == Phase::Preparing || other->phase == Phase::Running) &&
            pathsOverlap(other->scope.cwd, path))
            return true;
    }
    return false;
}

void Controller::connectThread(const QString &id)
{
    if (!threads_.contains(id) || threads_[id].archived || runtime_.value(id).phase != Phase::Offline)
        return;
    try {
        const auto &thread = threads_[id];
        const auto root = projectRoot(thread.project);
        const auto program = executable();
        const auto extension = thread.useCua ? driver() : QString();
        if (program.isEmpty())
            throw Failure("找不到 agy。请在设置中指定 Antigravity CLI。");
        if (thread.useCua && extension.isEmpty())
            throw Failure("找不到此会话启用的 Cua Driver。");
        auto &run = runtime_[id];
        run.phase = Phase::Connecting;
        run.status = thread.isolated ? "正在准备独立工作树…" : "正在连接 Antigravity…";
        run.cancelPending = false;
        emit threadChanged(id);
        runJob(this, [root, directory = profile_.directory(), id, isolated = thread.isolated] {
            return openScope(root, directory, id, isolated, true);
        }, [this, id, program, extension](const JobResult<Scope> &result) {
            if (shuttingDown_ || runtime_.value(id).phase != Phase::Connecting)
                return;
            if (runtime_[id].cancelPending) {
                abortConnection(id, "已取消连接。");
            } else if (!result.value) {
                abortConnection(id, result.error);
            } else {
                attachClient(id, *result.value, program, extension);
            }
        });
    } catch (const Failure &error) {
        abortConnection(id, error.text);
    }
}

void Controller::attachClient(const QString &id, const Scope &scope, const QString &program, const QString &driver)
{
    auto &run = runtime_[id];
    run.scope = scope;
    run.status = "正在连接 Antigravity…";
    auto *client = new AcpClient(program, scope.cwd, this);
    run.client = client;
    emit threadChanged(id);

    connect(client, &AcpClient::connectionError, this, [this, id](const auto &error) { abortConnection(id, error); });
    connect(client, &AcpClient::disconnected, this, [this, id](const auto &error) { disconnected(id, error); });
    connect(client, &AcpClient::needsAuthentication, this, [this, id](bool available) {
        auto &run = runtime_[id];
        run.phase = Phase::NeedAuth;
        run.authAvailable = available;
        if (!available) {
            abortConnection(id, "请先在终端运行 agy 登录，再重新连接。草稿已保留。");
            return;
        }
        run.status = "等待 Google 登录";
        emit threadChanged(id);
        emit navigationChanged();
    });
    connect(client, &AcpClient::connected, this, [this, id](const QString &remote, const QList<Message> &history) {
        auto &thread = threads_[id];
        auto &run = runtime_[id];
        thread.remote = remote;
        if (thread.messages.isEmpty() && !history.isEmpty())
            thread.messages = history;
        run.status = "已连接";
        run.phase = Phase::Ready;
        run.authAvailable = false;
        saveNow(id);
        emit threadChanged(id);
        emit transcriptChanged(id);
        if (run.source != SendSource::None)
            beginSend(id);
    });
    const auto changed = [this, id] { emit threadChanged(id); };
    connect(client, &AcpClient::optionsChanged, this, changed);
    connect(client, &AcpClient::permissionsChanged, this, [this, id] {
        emit navigationChanged();
        emit threadChanged(id);
    });
    connect(client, &AcpClient::delta, this, [this, id](const QString &text) {
        auto &thread = threads_[id];
        const auto index = runtime_[id].replyIndex;
        if (index >= 0 && index < thread.messages.size()) {
            thread.messages[index].text += text;
            saveLater(id);
            emit transcriptChanged(id);
        }
    });
    connect(client, &AcpClient::plan, this, [this, id](const QJsonArray &entries) {
        auto &thread = threads_[id];
        thread.plan = {};
        for (const auto &entry : entries) {
            if (thread.plan.size() == 128)
                break;
            const auto row = entry.toObject();
            thread.plan.append(QJsonObject{{"content", row.value("content").toString().left(1000)},
                                           {"status", row.value("status").toString().left(80)}});
        }
        saveLater(id);
        emit transcriptChanged(id);
    });
    connect(client, &AcpClient::tool, this, [this, id](const QJsonObject &tool) {
        auto &thread = threads_[id];
        const auto index = runtime_[id].replyIndex;
        if (index < 0 || index >= thread.messages.size())
            return;
        const auto toolId = tool.value("toolCallId").toString();
        auto &log = thread.messages[index].tools;
        QJsonObject row{{"id", toolId}, {"title", tool.value("title").toString("工具操作").left(600)},
                        {"status", tool.value("status").toString().left(80)}};
        bool found = false;
        for (qsizetype i = 0; i < log.size(); ++i) {
            const auto old = log.at(i).toObject();
            if (old.value("id").toString() != toolId || toolId.isEmpty())
                continue;
            if (!tool.contains("title"))
                row.insert("title", old.value("title"));
            if (!tool.contains("status"))
                row.insert("status", old.value("status"));
            log.replace(i, row);
            found = true;
            break;
        }
        if (!found) {
            if (log.size() == 250)
                log.removeFirst();
            log.append(row);
        }
        saveLater(id);
        emit transcriptChanged(id);
    });
    connect(client, &AcpClient::fileChange, this, [this, id](const QString &path, const QString &before, const QString &after) {
        auto &diffs = threads_[id].diffs;
        if (diffs.size() >= 64 && !diffs.contains(path))
            return;
        const auto earlier = diffs.value(path).toObject();
        diffs.insert(path, QJsonObject{{"before", earlier.value("before").toString(before)}, {"after", after}});
        saveLater(id);
        emit transcriptChanged(id);
    });
    connect(client, &AcpClient::notice, this, &Controller::statusMessage);
    connect(client, &AcpClient::turnEnded, this, [this, id](const auto &reason, const auto &error) { endTurn(id, reason, error); });
    connect(client, &AcpClient::steeringEnded, this, [this, id](const QString &text, const QString &error) {
        auto &run = runtime_[id];
        auto &thread = threads_[id];
        run.steering.clear();
        if (error.isEmpty()) {
            const Message note{"steer", text, {}, {}};
            const auto position = run.replyIndex;
            if (position >= 0) {
                thread.messages.insert(position, note);
                ++run.replyIndex;
            } else {
                thread.messages.append(note);
            }
            if (thread.draft.text == text)
                thread.draft.text.clear();
        } else {
            if (!thread.draft.text.contains(text))
                thread.draft.text = text + (thread.draft.text.isEmpty() ? QString() : "\n\n" + thread.draft.text);
            emit statusMessage(error);
        }
        saveNow(id);
        emit threadChanged(id);
        emit transcriptChanged(id);
    });
    const auto &thread = threads_[id];
    client->open(thread.remote, thread.messages.isEmpty(), driver);
}

void Controller::authenticate(const QString &id)
{
    auto &run = runtime_[id];
    if (run.phase != Phase::NeedAuth || !run.client || !run.authAvailable)
        return;
    run.phase = Phase::Connecting;
    run.status = "正在等待浏览器确认登录…";
    emit threadChanged(id);
    run.client->authenticate();
}

void Controller::send(const QString &id, SendSource source)
{
    if (!threads_.contains(id) || threads_[id].archived || runtime_.value(id).busy())
        return;
    const auto &thread = threads_[id];
    auto &run = runtime_[id];
    const auto draft = source == SendSource::Queue ? thread.queued : thread.draft;
    if (draft.empty())
        return;
    if (draft.text.size() > 100000) {
        emit statusMessage("消息超过 100,000 个字符。");
        return;
    }
    run.outbound = draft;
    run.source = source;
    if (run.phase == Phase::Ready)
        beginSend(id);
    else if (run.phase == Phase::Offline)
        connectThread(id);
    else
        emit threadChanged(id);
}

void Controller::beginSend(const QString &id)
{
    auto &run = runtime_[id];
    auto &thread = threads_[id];
    if (!run.client || run.phase != Phase::Ready || run.source == SendSource::None)
        return;
    const bool cuaBusy = thread.useCua && !driverCheckAllowed();
    if (conflicting(id, run.scope.cwd) || cuaBusy) {
        run.status = cuaBusy ? "桌面控制正在被另一项操作使用。" : "项目目录正由另一会话或交付操作使用。";
        resetPending(run);
        run.autoRunQueued = false;
        emit threadChanged(id);
        emit statusMessage(run.status);
        return;
    }
    run.phase = Phase::Preparing;
    run.status = "正在准备消息…";
    run.cancelPending = false;
    run.cancelStaging = std::make_shared<std::atomic_bool>(false);
    const auto cancelStaging = run.cancelStaging;
    const auto outbound = run.outbound;
    const auto cwd = run.scope.cwd;
    const auto images = run.client->supportsImages();
    emit navigationChanged();
    emit threadChanged(id);
    runJob(this, [outbound, cwd, images, cancelStaging] {
        auto batch = stageAttachments(cwd, outbound.files, cancelStaging.get());
        try {
            const auto text = outbound.text.trimmed().isEmpty() ? QString("请查看我附带的资料。") : outbound.text;
            auto blocks = promptContent(text, cwd, batch, images);
            if (cancelStaging->load(std::memory_order_relaxed))
                throw Failure("附件准备已取消。");
            return PreparedPrompt{batch, blocks};
        } catch (...) {
            if (!batch.directory.isEmpty())
                QDir(batch.directory).removeRecursively();
            throw;
        }
    }, [this, id, outbound, cancelStaging](const JobResult<PreparedPrompt> &result) {
        auto &run = runtime_[id];
        const bool current = run.cancelStaging == cancelStaging;
        if (!current || shuttingDown_ || run.phase != Phase::Preparing || !run.client || run.cancelPending || !result.value) {
            if (result.value && !result.value->batch.directory.isEmpty())
                QDir(result.value->batch.directory).removeRecursively();
            if (current && run.phase == Phase::Preparing) {
                run.phase = run.client && run.client->ready() ? Phase::Ready : Phase::Offline;
                run.status = run.cancelPending ? "已取消发送，草稿已保留。" : result.error;
                resetPending(run);
                run.autoRunQueued = false;
                emit threadChanged(id);
                emit navigationChanged();
                if (!run.status.isEmpty())
                    emit statusMessage(run.status);
            }
            return;
        }
        run.cancelStaging.reset();
        auto &thread = threads_[id];
        const bool queued = run.source == SendSource::Queue;
        Message message{"user", outbound.text, {}, {}};
        for (const auto &file : result.value->batch.files)
            message.attachments << file.name;
        const auto oldTitle = thread.title;
        if (thread.title == "新会话")
            thread.title = titleFor(outbound.text);
        thread.messages.append(message);
        thread.messages.append({"assistant", {}, {}, {}});
        run.replyIndex = thread.messages.size() - 1;
        const auto previousDraft = queued ? thread.queued : thread.draft;
        auto &sourceDraft = queued ? thread.queued : thread.draft;
        if (sameDraft(sourceDraft, outbound))
            sourceDraft = {};
        const auto previousPlan = std::exchange(thread.plan, {});
        const bool previouslyInterrupted = thread.interrupted;
        thread.interrupted = true;
        thread.updated = QDateTime::currentDateTimeUtc();
        if (!saveNow(id)) {
            thread.title = oldTitle;
            thread.messages.removeLast();
            thread.messages.removeLast();
            sourceDraft = previousDraft;
            thread.plan = previousPlan;
            thread.interrupted = previouslyInterrupted;
            run.replyIndex = -1;
            run.phase = Phase::Ready;
            run.status = "未能保存会话，消息保留在草稿中。";
            resetPending(run);
            run.autoRunQueued = false;
            if (!result.value->batch.directory.isEmpty())
                QDir(result.value->batch.directory).removeRecursively();
            emit navigationChanged();
            emit threadChanged(id);
            return;
        }
        run.queueInFlight = queued;
        run.phase = Phase::Running;
        run.status = "运行中";
        if (result.value->batch.skipped)
            emit statusMessage(QString("附件已跳过 %1 个链接、依赖或常见凭据条目。").arg(result.value->batch.skipped));
        run.client->prompt(result.value->blocks);
        emit navigationChanged();
        emit threadChanged(id);
        emit transcriptChanged(id);
    });
}

void Controller::steer(const QString &id)
{
    if (!threads_.contains(id))
        return;
    auto &run = runtime_[id];
    const auto &draft = threads_[id].draft;
    if (run.phase != Phase::Running || !run.client || !run.client->canSteer() ||
        !draft.files.isEmpty() || draft.text.trimmed().isEmpty() || draft.text.size() > 100000)
        return;
    run.steering = draft.text;
    run.client->steer(draft.text);
    emit threadChanged(id);
}

void Controller::stop(const QString &id)
{
    if (!runtime_.contains(id))
        return;
    auto &run = runtime_[id];
    run.autoRunQueued = false;
    if (run.phase == Phase::Running && run.client) {
        run.status = "正在停止…";
        run.client->cancel();
        emit threadChanged(id);
    } else if (run.phase == Phase::Preparing || (run.phase == Phase::Connecting && !run.client)) {
        run.cancelPending = true;
        if (run.cancelStaging)
            run.cancelStaging->store(true, std::memory_order_relaxed);
        run.status = "正在取消准备…";
        emit threadChanged(id);
    } else if (run.phase == Phase::Connecting || run.phase == Phase::NeedAuth) {
        abortConnection(id, "连接已取消，草稿已保留。");
    }
}

void Controller::decide(const QString &id, const QString &key, bool allow)
{
    auto client = runtime_.value(id).client;
    if (client)
        client->decide(key, allow);
}

void Controller::configure(const QString &id, const QString &option, const QString &value)
{
    const auto run = runtime_.value(id);
    if (run.phase == Phase::Ready && run.client)
        run.client->configure(option, value);
}

void Controller::resetPending(Runtime &run)
{
    run.source = SendSource::None;
    run.outbound = {};
    run.cancelPending = false;
    if (run.cancelStaging) {
        run.cancelStaging->store(true, std::memory_order_relaxed);
        run.cancelStaging.reset();
    }
    run.queueInFlight = false;
    run.authAvailable = false;
}

void Controller::abortConnection(const QString &id, const QString &error)
{
    closeClient(id);
    runtime_[id].status = error;
    emit navigationChanged();
    emit threadChanged(id);
    emit statusMessage(error);
}

void Controller::disconnected(const QString &id, const QString &error)
{
    auto &run = runtime_[id];
    const bool running = run.phase == Phase::Running;
    closeClient(id);
    run.status = running ? "连接中断，本轮结果未确认。" : error;
    if (running) {
        threads_[id].interrupted = true;
        saveNow(id);
        emit transcriptChanged(id);
    }
    emit navigationChanged();
    emit threadChanged(id);
    emit statusMessage(error);
}

void Controller::closeClient(const QString &id)
{
    auto &run = runtime_[id];
    if (run.client) {
        auto *client = run.client.data();
        disconnect(client, nullptr, this, nullptr);
        if (client->active())
            client->cancel();
        client->close();
        client->deleteLater();
        run.client.clear();
    }
    run.phase = Phase::Offline;
    run.status.clear();
    run.replyIndex = -1;
    run.steering.clear();
    run.autoRunQueued = false;
    resetPending(run);
}

void Controller::endTurn(const QString &id, const QString &reason, const QString &error)
{
    auto &run = runtime_[id];
    auto &thread = threads_[id];
    const bool retryAllowed = !error.isEmpty() && run.replyIndex == thread.messages.size() - 1 &&
                              run.replyIndex >= 0 && thread.messages.last().text.isEmpty() &&
                              thread.messages.last().tools.isEmpty();
    if (run.replyIndex == thread.messages.size() - 1 && run.replyIndex >= 0 &&
        thread.messages.last().text.isEmpty() && thread.messages.last().tools.isEmpty())
        thread.messages.removeLast();
    if (!error.isEmpty()) {
        if (retryAllowed) {
            auto &target = run.queueInFlight ? thread.queued : thread.draft;
            if (target.empty())
                target = run.outbound;
        }
        thread.messages.append({"system", error, {}, {}});
    }
    const bool next = error.isEmpty() && reason == "end_turn" && run.autoRunQueued && !thread.queued.empty();
    run.autoRunQueued = false;
    run.phase = Phase::Ready;
    run.replyIndex = -1;
    thread.interrupted = false;
    if (!error.isEmpty())
        run.status = "请求失败，详情已保留。";
    else if (reason == "cancelled")
        run.status = "已停止";
    else if (reason == "end_turn")
        run.status = "本轮完成";
    else
        run.status = "本轮结束 · " + reason;
    resetPending(run);
    saveNow(id);
    emit navigationChanged();
    emit threadChanged(id);
    emit transcriptChanged(id);
    if (next && !shuttingDown_) {
        const Draft expected = thread.queued;
        QTimer::singleShot(0, this, [this, id, expected] {
            if (!shuttingDown_ && threads_.contains(id) && sameDraft(threads_[id].queued, expected))
                send(id, SendSource::Queue);
        });
    }
}

void Controller::listRemote(const QString &projectId)
{
    if (historyClient_)
        return;
    try {
        const auto program = executable();
        if (program.isEmpty())
            throw Failure("找不到 agy。");
        auto *client = new AcpClient(program, projectRoot(projectId), this);
        historyClient_ = client;
        const auto dispose = [this, client] {
            disconnect(client, nullptr, this, nullptr);
            client->close();
            client->deleteLater();
            historyClient_.clear();
        };
        connect(client, &AcpClient::sessionsListed, this, [this, dispose, projectId](const auto &threads) {
            // The list belongs to the client, which is scheduled for disposal.
            const auto copy = threads;
            dispose();
            emit remoteSessions(projectId, copy);
        });
        const auto failed = [this, dispose](const QString &message) {
            dispose();
            emit remoteFailed(message);
        };
        connect(client, &AcpClient::connectionError, this, failed);
        connect(client, &AcpClient::disconnected, this, failed);
        connect(client, &AcpClient::needsAuthentication, this, [this, failed](bool available) {
            if (available)
                emit remoteAuthentication(true);
            else
                failed("请先在终端运行 agy 完成登录。");
        });
        client->listSessions();
    } catch (const Failure &error) {
        emit remoteFailed(error.text);
    }
}

void Controller::authenticateRemote()
{
    if (historyClient_)
        historyClient_->authenticate();
}

void Controller::cancelRemote()
{
    if (!historyClient_)
        return;
    auto *client = historyClient_.data();
    disconnect(client, nullptr, this, nullptr);
    client->close();
    client->deleteLater();
    historyClient_.clear();
}

int Controller::importRemote(const QString &projectId, const QList<RemoteThread> &sessions)
{
    const auto path = projectRoot(projectId);
    QSet<QString> known;
    for (const auto &thread : std::as_const(threads_)) {
        if (thread.project == projectId)
            known.insert(thread.remote);
    }
    int count = 0;
    for (const auto &session : sessions) {
        if (!samePath(session.root, path) || session.id.isEmpty() || known.contains(session.id))
            continue;
        Thread thread;
        thread.id = newId();
        thread.project = projectId;
        thread.remote = session.id;
        thread.title = session.title.trimmed().isEmpty() ? QString("导入的会话") : session.title.left(120);
        if (session.updated.isValid())
            thread.updated = session.updated;
        profile_.save(thread);
        threads_.insert(thread.id, thread);
        known.insert(session.id);
        ++count;
    }
    emit navigationChanged();
    return count;
}

int Controller::importLegacy(const QByteArray &json)
{
    flush();
    const auto count = profile_.importOrbit07(json);
    projects_ = profile_.projects();
    const auto loaded = profile_.threads();
    for (const auto &thread : loaded) {
        if (!threads_.contains(thread.id))
            threads_.insert(thread.id, thread);
    }
    emit navigationChanged();
    return count;
}

Scope Controller::scopeFor(const QString &id, bool requireIdle)
{
    if (!threads_.contains(id))
        throw Failure("请先选择会话。");
    const auto run = runtime_.value(id);
    if (requireIdle && (run.busy() || applying()))
        throw Failure("请等当前任务或交付操作完成。");
    const auto &thread = threads_[id];
    const auto root = projectRoot(thread.project);
    return {root, thread.isolated ? run.scope.cwd : root, run.scope.base, run.scope.branch};
}

void Controller::deliver(const QString &id, const Patch &patch)
{
    const auto scope = scopeFor(id, true);
    if (!threads_[id].isolated || conflicting(id, scope.root) ||
        (!scope.cwd.isEmpty() && conflicting(id, scope.cwd)))
        throw Failure("工作树或目标项目正在被其他操作使用。");
    mutationTarget_ = scope.root;
    emit threadChanged(id);
    runJob(this, [target = scope.root, patch] {
        applyPatch(target, patch);
        return patch.paths.size();
    }, [this, id](const JobResult<qsizetype> &result) {
        mutationTarget_.clear();
        emit threadChanged(id);
        emit statusMessage(result.value ? QString("已应用 %1 个文件，改动留在目标项目工作区。").arg(*result.value) :
                           "应用补丁失败：" + result.error);
    });
}

bool Controller::driverCheckAllowed() const
{
    if (driverTesting_)
        return false;
    for (auto run = runtime_.cbegin(); run != runtime_.cend(); ++run) {
        if ((run->phase == Phase::Preparing || run->phase == Phase::Running) && threads_.value(run.key()).useCua)
            return false;
    }
    return true;
}

void Controller::stopAll()
{
    if (shuttingDown_)
        return;
    shuttingDown_ = true;
    for (const auto &id : runtime_.keys())
        closeClient(id);
    if (historyClient_) {
        auto *client = historyClient_.data();
        disconnect(client, nullptr, this, nullptr);
        client->close();
    }
    flush();
}

}
