#pragma once

#include "controller.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QSyntaxHighlighter;

namespace orbite {

class ReviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit ReviewPanel(Controller &controller, QWidget *parent = nullptr);
    void bind(const QString &id);
    void refresh();
    void updateActivity();
    void updateActions();
    void updateTheme();
    void showActivity();

signals:
    void reference(const QString &path, int line);
    void dismissed();

private:
    enum class Delivery { Export, Apply };
    struct Filter {
        QString text;
        QString selection;
    };
    void filterWorkspace(bool reload = false);
    void filterSession();
    void loadFile(int row);
    void setSessionDiff();
    void findReference();
    void buildDelivery(Delivery action);
    void withScope(const std::function<void(const Scope &)> &ready);
    void reveal();

    Controller &controller_;
    QString id_;
    Review review_;
    QHash<QString, Filter> workspaceFilters_;
    QHash<QString, Filter> sessionFilters_;
    QString patchPath_;
    QStringList sessionPaths_;
    qsizetype refreshSequence_ = 0;
    qsizetype fileSequence_ = 0;
    bool exporting_ = false;
    QTabWidget *tabs_ = nullptr;
    QLabel *summary_ = nullptr;
    QLineEdit *workspaceSearch_ = nullptr;
    QLineEdit *sessionSearch_ = nullptr;
    QListWidget *files_ = nullptr;
    QListWidget *changes_ = nullptr;
    QPlainTextEdit *patch_ = nullptr;
    QPlainTextEdit *sessionDiff_ = nullptr;
    QPlainTextEdit *activity_ = nullptr;
    QPushButton *reference_ = nullptr;
    QPushButton *export_ = nullptr;
    QPushButton *apply_ = nullptr;
    QPushButton *reveal_ = nullptr;
    QSyntaxHighlighter *highlighter_ = nullptr;
};

}
