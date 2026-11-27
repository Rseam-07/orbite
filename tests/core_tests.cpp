#include "acp.h"
#include "attachments.h"
#include "computer_use.h"
#include "workspace.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace orbite;

namespace {

QString fixture()
{
    return QCoreApplication::applicationDirPath() + "/orbite_fixture"
#ifdef Q_OS_WIN
           ".exe"
#endif
           ;
}

void createFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        throw Failure("Cannot write fixture file");
}

QByteArray git(const QString &root, const QStringList &args)
{
    const auto result = runCommand(findTool("git"), args, root, 10000, 1024 * 1024);
    if (result.code)
        throw Failure(QString::fromUtf8(result.error));
    return result.output;
}

QJsonArray textPrompt(const QString &text)
{
    return QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}};
}

}

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void profileMigrationAndDurability()
    {
        QTemporaryDir directory;
        QString project, thread;
        {
            Profile profile(directory.path());
            project = newId();
            thread = newId();
            profile.save(Project{project, directory.path()});
            Thread state;
            state.id = thread;
            state.project = project;
            state.pinned = state.isolated = state.interrupted = true;
            state.draft = {"草稿", {"/tmp/附件.png"}};
            state.queued = {"下一条", {}};
            state.messages.append({"assistant", "原来的回复", {}, {}});
            profile.save(state);
            const QByteArray legacy = R"({"projects":[{"path":"/tmp/旧项目"}],"threads":[
                {"id":"legacy-1","folder":"/tmp/旧项目","title":"旧会话","remote":"remote-1","isolated":true,
                 "messages":[{"role":"user","text":"旧正文","files":[{"name":"附件.txt"}]}]}]})";
            QCOMPARE(profile.importOrbit07(legacy), 1);
            QCOMPARE(profile.importOrbit07(legacy), 0);
        }
        Profile reopened(directory.path());
        const auto threads = reopened.threads();
        QVERIFY(threads.value(thread).pinned);
        QVERIFY(threads.value(thread).interrupted);
        QCOMPARE(threads.value(thread).draft.files.value(0), "/tmp/附件.png");
        QCOMPARE(threads.value(thread).queued.text, "下一条");
        QCOMPARE(threads.value("legacy-1").remote, "remote-1");
        QCOMPARE(threads.value("legacy-1").messages.at(0).attachments.at(0), "附件.txt");
        QCOMPARE(reopened.setting("unconfirmedProject/" + threads.value("legacy-1").project), "yes");
    }

    void worktreeDeliversNewFilesWithoutChangingIndex()
    {
        QTemporaryDir directory;
        const auto parent = QDir(directory.path()).canonicalPath();
        const auto root = parent + "/repo";
        const auto profile = parent + "/profile";
        QVERIFY(QDir().mkpath(root));
        QVERIFY(QDir().mkpath(profile));
        git(root, {"init", "-q"});
        createFile(root + "/existing.txt", "old\n");
        createFile(root + "/unrelated.txt", "old\n");
        git(root, {"add", "."});
        git(root, {"-c", "user.name=Orbite", "-c", "user.email=orbite@example.test", "commit", "-qm", "base"});
        const auto first = openScope(root, profile, "task-1", true, true);
        const auto second = openScope(root, profile, "task-2", true, true);
        QVERIFY(first.cwd != second.cwd);
        QCOMPARE(openScope(root, profile, "task-1", true, false).base, first.base);
        createFile(root + "/unrelated.txt", "user draft\n");
        createFile(first.cwd + "/existing.txt", "updated\n");
        createFile(first.cwd + "/测试 new.txt", "new\n");
#ifndef Q_OS_WIN
        createFile(first.cwd + "/line\nbreak.txt", "odd\n");
#endif
        const auto binary = QByteArray::fromHex("00deadbeef00");
        createFile(first.cwd + "/sample.bin", binary);
        QVERIFY(QDir().mkpath(first.cwd + "/.orbit-attachments"));
        createFile(first.cwd + "/.orbit-attachments/secret.txt", "private\n");
        const auto diff = readReview(first);
        const auto n = std::find_if(diff.files.begin(), diff.files.end(), [](const auto &file) { return file.path == "测试 new.txt"; });
        QVERIFY(n != diff.files.end());
        QVERIFY(readPatch(first, *n).contains("+new"));
        const auto statusBefore = git(first.cwd, {"status", "--porcelain=v1", "-z"});
        const auto patch = buildPatch(first);
        QVERIFY(patch.paths.contains("existing.txt"));
        QVERIFY(patch.paths.contains("测试 new.txt"));
        QVERIFY(patch.paths.contains("sample.bin"));
#ifndef Q_OS_WIN
        QVERIFY(patch.paths.contains("line\nbreak.txt"));
#endif
        QVERIFY(!patch.data.contains("secret.txt"));
        QCOMPARE(statusBefore, git(first.cwd, {"status", "--porcelain=v1", "-z"}));
        QVERIFY(git(root, {"diff", "--cached"}).isEmpty());
        createFile(root + "/existing.txt", "user change\n");
        QVERIFY_THROWS_EXCEPTION(Failure, checkApply(root, patch));
        createFile(root + "/existing.txt", "old\n");
        applyPatch(root, patch);
        QVERIFY(QFileInfo::exists(root + "/测试 new.txt"));
        QFile actual(root + "/sample.bin");
        QVERIFY(actual.open(QIODevice::ReadOnly));
        QCOMPARE(actual.readAll(), binary);
        QVERIFY(git(root, {"diff", "--cached"}).isEmpty());
        QVERIFY(git(root, {"diff", "--", "unrelated.txt"}).contains("user draft"));
    }

    void attachmentsProtectTheSource()
    {
        QTemporaryDir directory;
        const auto root = QDir(directory.path()).canonicalPath();
        QVERIFY(QDir().mkdir(root + "/incoming"));
        createFile(root + "/incoming/图.txt", "资料\n");
        createFile(root + "/incoming/.env", "secret");
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(root + "/incoming/图.txt", root + "/incoming/link"));
#endif
        std::atomic_bool cancelled(true);
        QVERIFY_THROWS_EXCEPTION(Failure, stageAttachments(root, {root + "/incoming"}, &cancelled));
        QVERIFY(!QFileInfo::exists(root + "/.orbit-attachments"));
        const auto batch = stageAttachments(root, {root + "/incoming"});
        const auto staged = batch.files.first().path;
        QVERIFY(QFileInfo::exists(staged + "/图.txt"));
        QVERIFY(!QFileInfo::exists(staged + "/.env"));
        QVERIFY(!QFileInfo::exists(staged + "/link"));
        QVERIFY(QFileInfo::exists(root + "/incoming/.env"));
        QVERIFY(promptContent("检查资料", root, batch, true).first().toObject().value("text").toString()
                .contains(".orbit-attachments/turn-"));
    }

    void rpcRejectsBadFramesAndExpiresRequests()
    {
        QTemporaryDir tmp;
        for (const auto &method : QStringList{"malformed", "overflow", "hang"}) {
            RpcProcess rpc(nullptr, 1024);
            int callbacks = 0;
            QString error;
            QSignalSpy closed(&rpc, &RpcProcess::disconnected);
            connect(&rpc, &RpcProcess::started, &rpc, [&] {
                rpc.request(method, {}, [&](const auto &, const RpcError &result) {
                    ++callbacks;
                    error = result.text;
                }, method == "hang" ? 160 : 2500);
            });
            rpc.start(fixture(), {"acp", "--sandbox"}, tmp.path());
            QTRY_COMPARE_WITH_TIMEOUT(callbacks, 1, 3000);
            QVERIFY(!error.isEmpty());
            QCOMPARE(closed.size(), method == "hang" ? 0 : 1);
            rpc.close("test");
            QCOMPARE(callbacks, 1);
        }
    }

    void authenticationAndHistory()
    {
        QTemporaryDir tmp;
        QVERIFY(QDir(tmp.path()).mkdir("auth"));
        AcpClient client(fixture(), tmp.filePath("auth"));
        QSignalSpy auth(&client, &AcpClient::needsAuthentication);
        QList<Message> messages;
        connect(&client, &AcpClient::connected, &client, [&](const auto &, const auto &history) { messages = history; });
        client.open("legacy", true);
        QTRY_COMPARE_WITH_TIMEOUT(auth.size(), 1, 4000);
        QVERIFY(auth.first().first().toBool());
        client.authenticate();
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 4000);
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.first().text, "旧问题");
        QCOMPARE(client.options().first().choices.size(), 2);
        QCOMPARE(client.commands().first().first, "plan");
        client.configure("model", "reasoning");
        QTRY_COMPARE_WITH_TIMEOUT(client.options().first().value, "reasoning", 2000);
    }

    void restoresSavedSessionsWithoutReplay()
    {
        QTemporaryDir tmp;
        for (const auto &remote : QStringList{"saved-session", "load-only"}) {
            AcpClient client(fixture(), tmp.path());
            QList<Message> replay;
            connect(&client, &AcpClient::connected, &client, [&](const auto &, const auto &history) { replay = history; });
            client.open(remote, false);
            QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 4000);
            QCOMPARE(client.options().first().value, remote == "load-only" ? "test" : "reasoning");
            QVERIFY(replay.isEmpty());
        }
    }

    void reviewBeforeFirstCommit()
    {
        QTemporaryDir directory;
        const auto root = QDir(directory.path()).canonicalPath();
        git(root, {"init", "-q"});
        createFile(root + "/staged.txt", "first change\n");
        git(root, {"add", "staged.txt"});
        QVERIFY(QDir().mkdir(root + "/.orbit-attachments"));
        createFile(root + "/.orbit-attachments/staged.txt", "copied attachment");
        const auto scope = openScope(root, root, "initial", false, false);
        const auto review = readReview(scope);
        QCOMPARE(review.files.size(), 1);
        QCOMPARE(review.files.first().path, "staged.txt");
        QVERIFY(readPatch(scope, review.files.first()).contains("+first change"));
        QVERIFY(QDir().mkpath(root + "/sub/.orbit-attachments"));
        createFile(root + "/sub/staged.txt", "nested change\n");
        createFile(root + "/sub/.orbit-attachments/skip.txt", "private\n");
        git(root, {"add", "sub"});
        const auto sub = openScope(root + "/sub", root, "sub", false, false);
        const auto children = readReview(sub);
        QCOMPARE(children.files.size(), 1);
        QCOMPARE(children.files.first().path, "staged.txt");
        QVERIFY(readPatch(sub, children.files.first()).contains("+nested change"));
    }

    void historyPagination()
    {
        QTemporaryDir tmp;
        AcpClient client(fixture(), tmp.path());
        QList<RemoteThread> sessions;
        connect(&client, &AcpClient::sessionsListed, &client, [&](const auto &rows) { sessions = rows; });
        client.listSessions();
        QTRY_COMPARE_WITH_TIMEOUT(sessions.size(), 2, 4000);
        QCOMPARE(sessions.last().id, "two");
    }

    void isolatedCallbacksSteeringAndCancel()
    {
        QTemporaryDir tmp;
        AcpClient first(fixture(), tmp.path()), second(fixture(), tmp.path());
        QSignalSpy a(&first, &AcpClient::delta), b(&second, &AcpClient::delta);
        QSignalSpy done(&first, &AcpClient::turnEnded), secondDone(&second, &AcpClient::turnEnded);
        QSignalSpy steered(&first, &AcpClient::steeringEnded);
        first.open({}, false);
        second.open({}, false);
        QTRY_VERIFY_WITH_TIMEOUT(first.ready() && second.ready(), 4000);
        first.prompt(textPrompt("wait"));
        second.prompt(textPrompt("hi"));
        QTRY_COMPARE_WITH_TIMEOUT(secondDone.size(), 1, 4000);
        QCOMPARE(b.first().first().toString(), "完成。");
        QVERIFY(first.active());
        first.steer("补充");
        QTRY_COMPARE_WITH_TIMEOUT(steered.size(), 1, 4000);
        QVERIFY(first.active());
        first.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 4000);
        QCOMPARE(done.first().first().toString(), "cancelled");
        QCOMPARE(a.size(), 2);
    }

    void liveAntigravityRoundTrip()
    {
        const auto live = qEnvironmentVariable("ORBITE_LIVE_AGY");
        if (live.isEmpty())
            QSKIP("Set ORBITE_LIVE_AGY to enable the live ACP smoke test.");
        QTemporaryDir workspace;
        AcpClient client(live, workspace.path());
        QSignalSpy auth(&client, &AcpClient::needsAuthentication);
        QSignalSpy connected(&client, &AcpClient::connected);
        QSignalSpy failed(&client, &AcpClient::connectionError);
        QSignalSpy offline(&client, &AcpClient::disconnected);
        QSignalSpy done(&client, &AcpClient::turnEnded);
        QString reply;
        connect(&client, &AcpClient::delta, &client, [&](const auto &text) { reply += text; });
        connect(&client, &AcpClient::permissionsChanged, &client, [&] {
            for (const auto &key : client.permissions().keys())
                client.decide(key, false);
        });
        client.open({}, false);
        QTRY_VERIFY_WITH_TIMEOUT(client.ready() || !auth.isEmpty() || !failed.isEmpty() || !offline.isEmpty(), 30000);
        if (!auth.isEmpty())
            QSKIP("Antigravity CLI needs Google login in this environment.");
        if (!failed.isEmpty())
            QFAIL(qPrintable(failed.first().first().toString()));
        if (!offline.isEmpty())
            QFAIL(qPrintable(offline.first().first().toString()));
        QVERIFY(client.ready());
        QVERIFY(!client.options().isEmpty());
        client.prompt(textPrompt("仅用纯文本回复 ORBITE_QT_OK。无需调用工具。"));
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty() || !offline.isEmpty(), 90000);
        QVERIFY2(offline.isEmpty(), "Antigravity exited during live prompt.");
        QVERIFY2(done.first().at(1).toString().isEmpty(), qPrintable(done.first().at(1).toString()));
        QVERIFY2(reply.contains("ORBITE_QT_OK"), "Antigravity did not return the expected marker.");
        const auto session = connected.first().first().toString();
        client.close();
        AcpClient resumed(live, workspace.path());
        QList<Message> replay;
        connect(&resumed, &AcpClient::connected, &resumed, [&](const auto &, const auto &history) { replay = history; });
        resumed.open(session, true);
        QTRY_VERIFY_WITH_TIMEOUT(resumed.ready(), 50000);
        QVERIFY(std::any_of(replay.begin(), replay.end(), [](const auto &message) {
            return message.role == "assistant" && message.text.contains("ORBITE_QT_OK");
        }));
        resumed.close();
        AcpClient quiet(live, workspace.path());
        QList<Message> quietHistory;
        connect(&quiet, &AcpClient::connected, &quiet, [&](const auto &, const auto &history) { quietHistory = history; });
        quiet.open(session, false);
        QTRY_VERIFY_WITH_TIMEOUT(quiet.ready(), 50000);
        QVERIFY(quietHistory.isEmpty());
    }

    void liveDriverDiagnostics()
    {
        const auto driver = qEnvironmentVariable("ORBITE_LIVE_CUA");
        if (driver.isEmpty())
            QSKIP("Set ORBITE_LIVE_CUA to run read-only local Cua Driver diagnostics.");
        const auto report = inspectDriver(driver);
        qInfo().noquote() << "Cua Driver" << report.version << report.protocol << report.toolCount
                          << "tools;" << report.issues.join(" ");
        QVERIFY(!report.version.isEmpty());
        QVERIFY(!report.protocol.isEmpty());
        QVERIFY(report.missingTools.isEmpty());
    }

    void driverChecksToolPagination()
    {
        const auto report = inspectDriver(fixture());
        QCOMPARE(report.version, "0.62.1");
        QCOMPARE(report.toolCount, 5);
        QVERIFY(report.missingTools.isEmpty());
        QVERIFY(report.readOnlyPermissionCheck);
#ifdef Q_OS_MACOS
        QVERIFY(report.permissions.value("accessibility").toBool());
        QCOMPARE(report.permissionSource, "fixture");
#endif
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
