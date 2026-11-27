#pragma once

#include "controller.h"

#include <QMainWindow>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QFrame;
class QImage;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace orbite {

class ReviewPanel;
class Workbench;

class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(Controller &controller);
    void openPath(const QString &path);
    QString currentId() const { return current_; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void chooseProject();
    void createSession();
    void showHome();
    void selectThread(const QString &id);
    void rebuildNavigation();
    void syncThread();
    void scheduleTranscript();
    void renderTranscript();
    void paintApprovals(const Runtime &run);
    void appendFiles(const QStringList &paths, const QImage &image);
    void chooseAttachment(bool folder);
    void renderFiles(const Draft &draft);
    void buildModelMenu();
    void buildEffortMenu();
    void buildCommandMenu();
    void threadMenu(const QPoint &position, const QString &id);
    void withWorkspace(const std::function<void(const QString &)> &use);
    void showProjectFiles();
    void showTerminal();
    void insertReference(const QString &path, int line);
    void showSettings();
    void showExtension();
    void showRemote(const QString &project, const QList<RemoteThread> &rows);
    void importLegacy();
    void applyAppearance(const QString &choice);
    void displayReview(bool open);
    void info(const QString &message);
    void action(const std::function<void()> &execute);

    Controller &controller_;
    QString current_;
    QString project_;
    QString stageId_;
    QString transcriptId_;
    qsizetype transcriptLimit_ = 120;
    QString reviewId_;
    bool transcriptDeferred_ = false;
    Phase lastPhase_ = Phase::Offline;
    QTimer transcriptTimer_;
    QStringList displayedFiles_;
    QStringList permissionKeys_;
    QMap<QString, Permission> shownPermissions_;
    QTreeWidget *nav_ = nullptr;
    QLineEdit *search_ = nullptr;
    QCheckBox *archiveFilter_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QTextBrowser *transcript_ = nullptr;
    QLabel *projectLabel_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *status_ = nullptr;
    Workbench *home_ = nullptr;
    QWidget *welcome_ = nullptr;
    QWidget *welcomeSpace_ = nullptr;
    QPushButton *homeButton_ = nullptr;
    QPushButton *newButton_ = nullptr;
    QPushButton *moreButton_ = nullptr;
    QPushButton *reviewButton_ = nullptr;
    QFrame *composer_ = nullptr;
    QPlainTextEdit *input_ = nullptr;
    QWidget *files_ = nullptr;
    QPushButton *isolated_ = nullptr;
    QPushButton *add_ = nullptr;
    QPushButton *send_ = nullptr;
    QPushButton *stop_ = nullptr;
    QPushButton *queue_ = nullptr;
    QPushButton *connect_ = nullptr;
    QToolButton *model_ = nullptr;
    QToolButton *commands_ = nullptr;
    QToolButton *effort_ = nullptr;
    QMenu *modelMenu_ = nullptr;
    QMenu *commandMenu_ = nullptr;
    QMenu *effortMenu_ = nullptr;
    QFrame *queuedCard_ = nullptr;
    QLabel *queuedText_ = nullptr;
    QPushButton *runQueued_ = nullptr;
    QPushButton *restoreQueued_ = nullptr;
    QFrame *approvalCard_ = nullptr;
    QComboBox *approvalChoice_ = nullptr;
    QPlainTextEdit *approvalBody_ = nullptr;
    QPushButton *allow_ = nullptr;
    QPushButton *reject_ = nullptr;
    ReviewPanel *review_ = nullptr;
};

}
