#pragma once

#include <QLabel>
#include <QWidget>

class QFrame;
class QVBoxLayout;

namespace orbite {

class Controller;

class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(const QString &text, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

class Workbench : public QWidget {
    Q_OBJECT
public:
    Workbench(Controller &controller, QWidget *parent);
    void rebuild(const QString &project = {});

signals:
    void openProject();
    void projectSelected(const QString &id);
    void threadSelected(const QString &id);

private:
    Controller &controller_;
    QFrame *projectTable_ = nullptr;
    QFrame *recentTable_ = nullptr;
    QLabel *recentTitle_ = nullptr;
    QLabel *projectTitle_ = nullptr;
    QVBoxLayout *projects_ = nullptr;
    QVBoxLayout *recent_ = nullptr;
};

}
