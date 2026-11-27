#include "workbench.h"
#include "controller.h"
#include "theme.h"

#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace orbite {
namespace {

QString projectName(const Project &project)
{
    const auto name = QFileInfo(project.root).fileName();
    return name.isEmpty() ? project.root : name;
}

QString displayPath(const QString &path)
{
    const auto home = QDir::homePath();
    return path.startsWith(home + "/") ? "~" + path.mid(home.size()) : path;
}

QString recentTime(const QDateTime &time)
{
    const auto local = time.toLocalTime();
    const auto today = QDate::currentDate();
    if (local.date() == today)
        return local.toString("HH:mm");
    if (local.date() == today.addDays(-1))
        return "昨天";
    return local.toString("M月d日");
}

QLabel *label(const QString &text, const QString &name, QWidget *parent, bool elided = false)
{
    auto *value = elided ? new ElidedLabel(text, parent) : new QLabel(text, parent);
    value->setObjectName(name);
    value->setTextFormat(Qt::PlainText);
    value->setAttribute(Qt::WA_TransparentForMouseEvents);
    return value;
}

QLabel *iconLabel(Icon icon, QWidget *parent, int size = 17)
{
    auto *item = new QLabel(parent);
    item->setPixmap(lineIcon(icon, colors().accent).pixmap(QSize(size, size), 2));
    item->setAlignment(Qt::AlignCenter);
    item->setAttribute(Qt::WA_TransparentForMouseEvents);
    return item;
}

void clear(QVBoxLayout *layout)
{
    while (auto *entry = layout->takeAt(0)) {
        entry->widget()->hide();
        entry->widget()->deleteLater();
        delete entry;
    }
}

void divider(QVBoxLayout *layout, QWidget *parent)
{
    if (!layout->count())
        return;
    auto *rule = new QFrame(parent);
    rule->setObjectName("homeRule");
    rule->setFixedHeight(1);
    layout->addWidget(rule);
}

QPushButton *row(QWidget *table, QVBoxLayout *layout, const QString &title, int height)
{
    divider(layout, table);
    auto *cell = new QWidget(table);
    cell->setFixedHeight(height);
    auto *cellLayout = new QVBoxLayout(cell);
    cellLayout->setContentsMargins(0, 0, 0, 0);
    auto *button = new QPushButton(cell);
    button->setProperty("homeRow", true);
    button->setProperty("projectTile", height == 62);
    button->setAccessibleName(title);
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cellLayout->addWidget(button);
    layout->addWidget(cell);
    return button;
}

}

ElidedLabel::ElidedLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent)
{
    setTextFormat(Qt::PlainText);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setMinimumWidth(0);
}

void ElidedLabel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setFont(font());
    painter.setPen(palette().color(QPalette::WindowText));
    const auto area = contentsRect();
    painter.drawText(area, alignment() | Qt::TextSingleLine,
                     fontMetrics().elidedText(text(), objectName() == "homePath" ? Qt::ElideMiddle : Qt::ElideRight,
                                              qMax(0, area.width())));
}

Workbench::Workbench(Controller &controller, QWidget *parent)
    : QWidget(parent), controller_(controller)
{
    setObjectName("workbench");
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("workbenchScroll");
    scroll->setWidgetResizable(true);
    outer->addWidget(scroll);

    auto *body = new QWidget(scroll);
    body->setObjectName("workbench");
    auto *horizontal = new QHBoxLayout(body);
    horizontal->setContentsMargins(30, 34, 30, 24);
    horizontal->setSpacing(0);
    horizontal->addStretch(1);
    auto *content = new QWidget(body);
    content->setMaximumWidth(870);
    horizontal->addWidget(content, 100);
    horizontal->addStretch(1);
    scroll->setWidget(body);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *heading = new QHBoxLayout;
    heading->setSpacing(12);
    auto *logo = new QLabel(content);
    logo->setPixmap(QPixmap(":/assets/orbite-mark.png").scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    heading->addWidget(logo);
    heading->addWidget(label("工作台", "homeTitle", content), 1);
    auto *open = new QPushButton("打开项目", content);
    open->setCursor(Qt::PointingHandCursor);
    open->setProperty("primary", true);
    heading->addWidget(open);
    connect(open, &QPushButton::clicked, this, &Workbench::openProject);
    layout->addLayout(heading);
    layout->addSpacing(19);

    projectTitle_ = label("项目", "sectionTitle", content);
    layout->addWidget(projectTitle_);
    projectTable_ = new QFrame(content);
    projectTable_->setObjectName("homeTable");
    projects_ = new QVBoxLayout(projectTable_);
    projects_->setContentsMargins(5, 5, 5, 5);
    projects_->setSpacing(0);
    layout->addWidget(projectTable_);

    layout->addSpacing(15);
    recentTitle_ = label("最近会话", "sectionTitle", content);
    layout->addWidget(recentTitle_);
    recentTable_ = new QFrame(content);
    recentTable_->setObjectName("homeTable");
    recent_ = new QVBoxLayout(recentTable_);
    recent_->setContentsMargins(5, 5, 5, 5);
    recent_->setSpacing(0);
    layout->addWidget(recentTable_);
    layout->addStretch(1);
}

void Workbench::rebuild(const QString &project)
{
    clear(projects_);
    clear(recent_);
    auto ordered = controller_.projects().values();
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
        return projectName(a).localeAwareCompare(projectName(b)) < 0;
    });
    QList<const Thread *> threads;
    for (const auto &thread : controller_.threads()) {
        if (!thread.archived && (project.isEmpty() || thread.project == project))
            threads << &thread;
    }
    std::sort(threads.begin(), threads.end(), [](const auto *a, const auto *b) { return a->updated > b->updated; });

    projectTitle_->setVisible(project.isEmpty());
    projectTable_->setVisible(project.isEmpty());
    if (ordered.isEmpty()) {
        auto *empty = label("打开本地项目后，即可连接 Antigravity。", "homeMuted", projectTable_);
        empty->setContentsMargins(12, 17, 12, 17);
        projects_->addWidget(empty);
    }
    if (project.isEmpty()) {
        for (const auto &item : ordered) {
            const auto name = projectName(item);
            auto *button = row(projectTable_, projects_, name, 62);
            button->setToolTip(item.root);
            auto *layout = new QHBoxLayout(button);
            layout->setContentsMargins(10, 0, 9, 0);
            layout->setSpacing(11);
            auto *tile = iconLabel(Icon::Folder, button, 18);
            tile->setObjectName("homeTile");
            tile->setFixedSize(36, 36);
            layout->addWidget(tile);
            auto *details = new QVBoxLayout;
            details->setSpacing(2);
            details->addWidget(label(name, "homeProjectName", button, true));
            details->addWidget(label(displayPath(item.root), "homePath", button, true));
            layout->addLayout(details, 3);

            int count = 0;
            int running = 0;
            const Thread *last = nullptr;
            for (const auto *thread : threads) {
                if (thread->project != item.id)
                    continue;
                if (!last)
                    last = thread;
                ++count;
                if (controller_.runtime(thread->id).busy())
                    ++running;
            }
            if (last)
                layout->addWidget(label(last->title, "homeMuted", button, true), 2);
            layout->addWidget(label(running ? QString("%1 项运行中").arg(running) : QString("%1 个会话").arg(count),
                                    "homeMuted", button));
            layout->addWidget(iconLabel(Icon::Chevron, button, 13));
            connect(button, &QPushButton::clicked, this, [this, id = item.id] { emit projectSelected(id); });
        }
    }

    recentTitle_->setText(project.isEmpty() ? "最近会话" : projectName(controller_.projects().value(project)) + " 的最近会话");
    const bool showRecent = !threads.isEmpty() || !project.isEmpty();
    recentTitle_->setVisible(showRecent);
    recentTable_->setVisible(showRecent);
    if (threads.isEmpty() && showRecent) {
        auto *empty = label("此项目还没有会话。可以从上方新建。", "homeMuted", recentTable_);
        empty->setContentsMargins(12, 15, 12, 15);
        recent_->addWidget(empty);
    }
    const auto visible = threads.mid(0, 7);
    for (const auto *thread : visible) {
        auto *button = row(recentTable_, recent_, thread->title, 40);
        button->setToolTip(thread->title);
        auto *layout = new QHBoxLayout(button);
        layout->setContentsMargins(10, 0, 11, 0);
        layout->setSpacing(10);
        layout->addWidget(iconLabel(Icon::Chat, button, 15));
        layout->addWidget(label(thread->title, "homeRecentName", button, true), 3);
        if (project.isEmpty())
            layout->addWidget(label(projectName(controller_.projects().value(thread->project)), "homeMuted", button, true), 1);
        const auto run = controller_.runtime(thread->id);
        const auto time = run.client && !run.client->permissions().isEmpty() ? QString("待审批") :
                          run.busy() ? QString("运行中") : recentTime(thread->updated);
        layout->addWidget(label(time, "homeMuted", button));
        connect(button, &QPushButton::clicked, this, [this, id = thread->id] { emit threadSelected(id); });
    }
}

}
