#include "theme.h"
#include "window.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QCompleter>
#include <QDir>
#include <QDropEvent>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMimeData>
#include <QTabWidget>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QtTest>

using namespace orbite;

class DesktopTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qApp->setStyle("Fusion");
    }

    void projectToApprovalToDelivery()
    {
        QTemporaryDir temp;
        const auto root = QDir(temp.path()).canonicalPath();
        const auto workspace = root + "/Atlas";
        QVERIFY(QDir().mkdir(workspace));
        const auto git = [&](const QStringList &args) {
            const auto result = runCommand(findTool("git"), args, workspace, 10000, 1024 * 1024);
            if (result.code)
                throw Failure(QString::fromUtf8(result.error));
        };
        git({"init", "-q"});
        QFile readme(workspace + "/README.md");
        QVERIFY(readme.open(QIODevice::WriteOnly));
        readme.write("# Atlas\n");
        readme.close();
        git({"add", "."});
        git({"-c", "user.name=Orbite", "-c", "user.email=orbite@example.test", "commit", "-qm", "base"});
        Profile profile(root + "/profile");
        QString fixture = QCoreApplication::applicationDirPath() + "/orbite_fixture";
#ifdef Q_OS_WIN
        fixture += ".exe";
#endif
        Controller controller(profile, fixture);
        applyTheme(qEnvironmentVariable("ORBITE_PREVIEW_THEME", "dark"));
        const auto capture = qEnvironmentVariable("ORBITE_SCREENSHOT");
        const auto captureView = [&](Window &window, const QString &view) {
            if (capture.isEmpty())
                return true;
            const QFileInfo info(capture);
            QTest::qWait(150);
            return window.grab().save(info.dir().filePath(info.completeBaseName() + view + ".png"));
        };
        if (!capture.isEmpty()) {
            const auto notes = root + "/NotesKit";
            QVERIFY(QDir().mkdir(notes));
            const auto notesProject = controller.openProject(notes);
            const auto previous = controller.newThread(notesProject);
            controller.rename(previous, "查看导出流程");
            controller.rename(controller.newThread(notesProject), "整理发布清单");
        }
        Window window(controller);
        window.resize(1350, 820);
        window.show();
        window.openPath(workspace);
        const auto id = window.currentId();
        QVERIFY(!id.isEmpty());
        controller.setIsolated(id, true);
        QVERIFY(captureView(window, "-new-session"));
        auto *input = window.findChild<QPlainTextEdit *>("composerEdit");
        QVERIFY(input);
        input->setPlainText("为 Atlas 增加一份审阅笔记，方便随工作树补丁交付。");
        auto *send = window.findChild<QPushButton *>("sendPrompt");
        QVERIFY(send && send->isEnabled());
        QTest::mouseClick(send, Qt::LeftButton);
        auto *allow = window.findChild<QPushButton *>("allowOnce");
        QVERIFY(allow);
        QTRY_VERIFY_WITH_TIMEOUT(allow->isVisible() && allow->isEnabled(), 10000);
        QTest::mouseClick(allow, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(controller.runtime(id).phase, Phase::Ready, 5000);
        auto *transcript = window.findChild<QTextBrowser *>("transcript");
        QTRY_VERIFY_WITH_TIMEOUT(transcript->toPlainText().contains("已写入"), 5000);
        auto *effort = window.findChild<QToolButton *>("effortControl");
        QVERIFY(effort && effort->isVisible());
        QCOMPARE(effort->text(), "中等");
        controller.configure(id, "effort", "high");
        QTRY_COMPARE_WITH_TIMEOUT(effort->text(), "高", 3000);
        input->setFocus();
        QTest::keyClicks(input, "/pl");
        auto *suggestions = input->findChild<QCompleter *>();
        QVERIFY(suggestions);
        QTRY_VERIFY_WITH_TIMEOUT(suggestions->popup()->isVisible(), 3000);
        QTest::keyClick(suggestions->popup(), Qt::Key_Tab);
        QTRY_COMPARE_WITH_TIMEOUT(input->toPlainText(), "/plan ", 2000);
        input->clear();

        QVERIFY(!QFileInfo::exists(workspace + "/review-notes.txt"));
        auto *review = window.findChild<QPushButton *>("reviewToggle");
        QVERIFY(review);
        QTest::mouseClick(review, Qt::LeftButton);
        auto *patchView = window.findChild<QPlainTextEdit *>("gitPatch");
        QTRY_VERIFY_WITH_TIMEOUT(patchView->toPlainText().contains("Qt 工作树验收"), 5000);
        const auto *panel = window.findChild<QWidget *>("reviewPanel");
        QVERIFY(panel && panel->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(panel->width() >= 340, 3000);
        const auto *list = window.findChild<QListWidget *>("changedFiles");
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 5000);
        auto *filter = window.findChild<QLineEdit *>("workspaceFilter");
        QVERIFY(filter);
        filter->setText("different.txt");
        QCOMPARE(list->currentRow(), -1);
        QVERIFY(patchView->toPlainText().isEmpty());
        filter->setText("REVIEW");
        QTRY_VERIFY_WITH_TIMEOUT(patchView->toPlainText().contains("Qt 工作树验收"), 5000);
        filter->clear();
        auto *tabs = panel->findChild<QTabWidget *>();
        QVERIFY(tabs);
        tabs->setCurrentIndex(1);
        auto *sessionFilter = window.findChild<QLineEdit *>("sessionFilter");
        auto *sessionDiff = window.findChild<QPlainTextEdit *>("sessionDiff");
        QVERIFY(sessionFilter && sessionDiff);
        QTRY_VERIFY_WITH_TIMEOUT(sessionDiff->toPlainText().contains("Qt 工作树验收"), 5000);
        sessionFilter->setText("missing.txt");
        QVERIFY(sessionDiff->toPlainText().isEmpty());
        tabs->setCurrentIndex(0);
        QVERIFY(filter->text().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(list->isVisible() &&
            window.findChild<QLabel *>("reviewSummary")->text().contains("1 个变更"), 3000);
        QTest::qWait(200);
        QVERIFY(captureView(window, ""));
        auto *home = window.findChild<QPushButton *>("workbenchButton");
        QVERIFY(home);
        QTest::mouseClick(home, Qt::LeftButton);
        QVERIFY(window.currentId().isEmpty());
        QVERIFY(window.findChild<QWidget *>("workbench")->isVisible());
        const auto visibleTile = [&] {
            const auto rows = window.findChildren<QPushButton *>();
            return std::any_of(rows.begin(), rows.end(), [](const auto *row) {
                return row->property("projectTile").toBool() && row->isVisible() && row->height() >= 60;
            });
        };
        QTRY_VERIFY_WITH_TIMEOUT(visibleTile(), 3000);
        QVERIFY(captureView(window, "-workbench"));
        const auto patch = buildPatch(controller.scopeFor(id));
        controller.deliver(id, patch);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.applying(), 5000);
        QVERIFY(QFileInfo::exists(workspace + "/review-notes.txt"));
        window.close();
        QVERIFY(profile.threads().value(id).messages.last().text.contains("已写入"));
    }

    void queueAndDirectoryLockSurviveInterruption()
    {
        QTemporaryDir temp;
        const auto root = QDir(temp.path()).canonicalPath();
        const auto workspace = root + "/Project";
        QVERIFY(QDir().mkdir(workspace));
        Profile profile(root + "/profile");
        QString fixture = QCoreApplication::applicationDirPath() + "/orbite_fixture";
#ifdef Q_OS_WIN
        fixture += ".exe";
#endif
        Controller controller(profile, fixture);
        const auto project = controller.openProject(workspace);
        const auto first = controller.newThread(project);
        const auto second = controller.newThread(project);
        controller.setDraft(first, {"wait", {}});
        controller.send(first);
        QTRY_COMPARE_WITH_TIMEOUT(controller.runtime(first).phase, Phase::Running, 5000);
        controller.setDraft(second, {"第二个任务", {}});
        controller.send(second);
        QTRY_VERIFY_WITH_TIMEOUT(controller.runtime(second).status.contains("另一会话"), 5000);
        QCOMPARE(controller.thread(second)->draft.text, "第二个任务");

        controller.setDraft(first, {"下一次再发", {}});
        controller.queueDraft(first);
        QCOMPARE(controller.thread(first)->queued.text, "下一次再发");
        controller.stop(first);
        QTRY_COMPARE_WITH_TIMEOUT(controller.runtime(first).phase, Phase::Ready, 5000);
        QCOMPARE(controller.thread(first)->queued.text, "下一次再发");
        controller.send(second);
        QTRY_COMPARE_WITH_TIMEOUT(controller.thread(second)->messages.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.thread(second)->messages.last().text.contains("完成"), 5000);

        controller.setDraft(first, {"disconnect", {}});
        controller.send(first);
        QTRY_VERIFY_WITH_TIMEOUT(controller.runtime(first).status.contains("连接中断"), 5000);
        QCOMPARE(controller.thread(first)->messages.last().text, "部分结果");
        const auto saved = profile.threads().value(first);
        QVERIFY(saved.interrupted);
        QCOMPARE(saved.queued.text, "下一次再发");
    }

    void composerAcceptsFileAndFolderDropsAsCopy()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = QDir(temp.path()).canonicalPath();
        const auto workspace = root + "/Project";
        const auto other = root + "/Other project";
        const auto folder = workspace + "/附件 folder";
        const auto file = workspace + "/file [1].txt";
        QVERIFY(QDir().mkpath(folder + "/nested"));
        QVERIFY(QDir().mkpath(other));
        for (const auto &path : {file, folder + "/nested/同名.txt"}) {
            QFile contents(path);
            QVERIFY(contents.open(QIODevice::WriteOnly));
            QCOMPARE(contents.write("fixture"), qint64(7));
        }

        Profile profile(root + "/profile");
        Controller controller(profile);
        Window window(controller);
        window.show();
        window.openPath(workspace);
        const auto id = window.currentId();
        QVERIFY(!id.isEmpty());
        auto *frame = window.findChild<QWidget *>("composerCard");
        auto *editor = window.findChild<QPlainTextEdit *>("composerEdit");
        QVERIFY(frame && editor);
        editor->setPlainText("Keep the unsent draft");

        const auto verifyDrop = [](QWidget *target, const QMimeData &mime,
                                   Qt::DropActions actions, bool canCopy) {
            const QPoint point(2, 2);
            QDragEnterEvent enter(point, actions, &mime, Qt::LeftButton, Qt::ShiftModifier);
            QApplication::sendEvent(target, &enter);
            QCOMPARE(enter.isAccepted(), canCopy);
            if (!canCopy)
                return;
            QCOMPARE(enter.dropAction(), Qt::CopyAction);
            QDragMoveEvent move(point, actions, &mime, Qt::LeftButton, Qt::ShiftModifier);
            QApplication::sendEvent(target, &move);
            QVERIFY(move.isAccepted());
            QCOMPARE(move.dropAction(), Qt::CopyAction);
            QDropEvent drop(point, actions, &mime, Qt::LeftButton, Qt::ShiftModifier);
            QApplication::sendEvent(target, &drop);
            QVERIFY(drop.isAccepted());
            QCOMPARE(drop.dropAction(), Qt::CopyAction);
        };

        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(file), QUrl::fromLocalFile(folder),
                      QUrl::fromLocalFile(file), QUrl("https://example.test/remote")});
        verifyDrop(frame, mime, Qt::MoveAction, false);
        verifyDrop(editor->viewport(), mime, Qt::MoveAction, false);
        QVERIFY(controller.thread(id)->draft.files.isEmpty());
        verifyDrop(frame, mime, Qt::CopyAction | Qt::MoveAction, true);
        const QStringList expected{QFileInfo(file).canonicalFilePath(), QFileInfo(folder).canonicalFilePath()};
        QCOMPARE(controller.thread(id)->draft.files, expected);
        verifyDrop(editor->viewport(), mime, Qt::CopyAction | Qt::MoveAction, true);
        QCOMPARE(controller.thread(id)->draft.files, expected);
        QCOMPARE(editor->toPlainText(), "Keep the unsent draft");
        QVERIFY(controller.thread(id)->messages.isEmpty());
        QVERIFY(QFileInfo::exists(file));
        QVERIFY(QFileInfo::exists(folder + "/nested/同名.txt"));

        QMimeData remote;
        remote.setUrls({QUrl("https://example.test/remote")});
        verifyDrop(frame, remote, Qt::CopyAction, false);

        window.openPath(other);
        QVERIFY(window.currentId() != id);
        QVERIFY(controller.thread(window.currentId())->draft.empty());
        window.openPath(workspace);
        QCOMPARE(window.currentId(), id);
        QCOMPARE(editor->toPlainText(), "Keep the unsent draft");
        QCOMPARE(controller.thread(id)->draft.files, expected);
        window.close();
        QCOMPARE(profile.threads().value(id).draft.files, expected);
    }

    void clipboardScreenshotPersistsWithDraft()
    {
        QTemporaryDir temp;
        const auto root = QDir(temp.path()).canonicalPath();
        Profile profile(root + "/profile");
        Controller controller(profile);
        Window window(controller);
        window.show();
        window.openPath(root);
        QImage image(24, 18, QImage::Format_RGB32);
        image.fill(Qt::green);
        qApp->clipboard()->setImage(image);
        auto *input = window.findChild<QPlainTextEdit *>("composerEdit");
        input->paste();
        const auto id = window.currentId();
        const auto files = controller.thread(id)->draft.files;
        QCOMPARE(files.size(), 1);
        QVERIFY(QFileInfo(files.first()).isFile());
        QCOMPARE(QImage(files.first()).size(), image.size());
        window.close();
        QCOMPARE(profile.threads().value(id).draft.files, files);
    }
};

QTEST_MAIN(DesktopTests)
#include "desktop_tests.moc"
