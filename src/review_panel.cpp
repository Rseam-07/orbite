#include "review_panel.h"
#include "jobs.h"
#include "theme.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTextBlock>
#include <QUrl>

namespace orbite {
namespace {

class DiffColors : public QSyntaxHighlighter {
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString &line) override
    {
        const auto c = colors();
        QColor foreground = c.text;
        if (line.startsWith('+') && !line.startsWith("+++"))
            foreground = c.added;
        else if (line.startsWith('-') && !line.startsWith("---"))
            foreground = c.removed;
        else if (line.startsWith("@@") || line.startsWith("diff ") || line.startsWith("index "))
            foreground = c.muted;
        QTextCharFormat style;
        style.setForeground(foreground);
        setFormat(0, line.size(), style);
    }
};

QPlainTextEdit *reader(const QString &name, QWidget *parent)
{
    auto *edit = new QPlainTextEdit(parent);
    edit->setObjectName(name);
    edit->setReadOnly(true);
    return edit;
}

QLineEdit *filterBox(const QString &name, QWidget *parent)
{
    auto *edit = new QLineEdit(parent);
    edit->setObjectName(name);
    edit->setPlaceholderText("筛选文件名或路径");
    edit->setClearButtonEnabled(true);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), edit);
    escape->setContext(Qt::WidgetShortcut);
    QObject::connect(escape, &QShortcut::activated, edit, &QLineEdit::clear);
    return edit;
}

int selectFiltered(QListWidget *list, const QString &query, const QString &preferred)
{
    const QSignalBlocker blocked(list);
    int first = -1;
    int selected = -1;
    int matches = 0;
    for (int index = 0; index < list->count(); ++index) {
        auto *row = list->item(index);
        const auto path = row->data(Qt::UserRole).toString();
        const bool show = path.contains(query, Qt::CaseInsensitive);
        row->setHidden(!show);
        if (show) {
            if (first < 0)
                first = index;
            if (path == preferred)
                selected = index;
            ++matches;
        }
    }
    list->setCurrentRow(selected >= 0 ? selected : first);
    return matches;
}


}

ReviewPanel::ReviewPanel(Controller &controller, QWidget *parent)
    : QWidget(parent), controller_(controller)
{
    setObjectName("reviewPanel");
    setMinimumWidth(340);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 15, 16, 13);
    layout->setSpacing(11);
    auto *heading = new QHBoxLayout;
    auto *label = new QLabel("变更审阅", this);
    label->setObjectName("reviewTitle");
    auto *refreshButton = new QPushButton(this);
    auto *closeButton = new QPushButton(this);
    refreshButton->setAccessibleName("刷新审阅");
    refreshButton->setToolTip("刷新审阅");
    closeButton->setAccessibleName("收起审阅");
    closeButton->setToolTip("收起审阅");
    setLineIcon(refreshButton, Icon::Refresh);
    setLineIcon(closeButton, Icon::Close);
    refreshButton->setProperty("ghost", true);
    closeButton->setProperty("ghost", true);
    refreshButton->setProperty("small", true);
    closeButton->setProperty("small", true);
    heading->addWidget(label, 1);
    heading->addWidget(refreshButton);
    heading->addWidget(closeButton);
    layout->addLayout(heading);

    tabs_ = new QTabWidget(this);
    auto *workspace = new QWidget(tabs_);
    auto *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 8, 0, 0);
    summary_ = new QLabel(workspace);
    summary_->setObjectName("reviewSummary");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    workspaceLayout->addWidget(summary_);
    workspaceSearch_ = filterBox("workspaceFilter", workspace);
    workspaceLayout->addWidget(workspaceSearch_);
    auto *split = new QSplitter(Qt::Vertical, workspace);
    files_ = new QListWidget(split);
    files_->setObjectName("changedFiles");
    files_->setMinimumHeight(40);
    files_->setMaximumHeight(175);
    patch_ = reader("gitPatch", split);
    patch_->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monospace.setPointSize(11);
    patch_->setFont(monospace);
    highlighter_ = new DiffColors(patch_->document());
    split->setChildrenCollapsible(false);
    split->setSizes({112, 420});
    workspaceLayout->addWidget(split, 1);
    auto *actions = new QHBoxLayout;
    reference_ = new QPushButton("引用行", this);
    export_ = new QPushButton("导出补丁", this);
    apply_ = new QPushButton("应用到项目", this);
    apply_->setProperty("primary", true);
    actions->addWidget(reference_);
    actions->addStretch();
    actions->addWidget(export_);
    actions->addWidget(apply_);
    workspaceLayout->addLayout(actions);
    reveal_ = new QPushButton("显示工作树目录", workspace);
    reveal_->setProperty("ghost", true);
    setLineIcon(reveal_, Icon::Folder);
    workspaceLayout->addWidget(reveal_);
    tabs_->addTab(workspace, "工作区");

    auto *session = new QWidget(tabs_);
    auto *sessionLayout = new QVBoxLayout(session);
    sessionLayout->setContentsMargins(0, 8, 0, 0);
    sessionSearch_ = filterBox("sessionFilter", session);
    sessionLayout->addWidget(sessionSearch_);
    auto *sessionSplit = new QSplitter(Qt::Vertical, session);
    changes_ = new QListWidget(sessionSplit);
    sessionDiff_ = reader("sessionDiff", sessionSplit);
    sessionDiff_->setFont(monospace);
    sessionSplit->setChildrenCollapsible(false);
    sessionSplit->setSizes({145, 360});
    sessionLayout->addWidget(sessionSplit, 1);
    tabs_->addTab(session, "会话变更");

    activity_ = reader("activityText", tabs_);
    tabs_->addTab(activity_, "计划与工具");
    layout->addWidget(tabs_, 1);

    connect(refreshButton, &QPushButton::clicked, this, &ReviewPanel::refresh);
    connect(closeButton, &QPushButton::clicked, this, &ReviewPanel::dismissed);
    connect(files_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            workspaceFilters_[id_].selection = files_->item(row)->data(Qt::UserRole).toString();
        loadFile(row);
    });
    connect(changes_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            sessionFilters_[id_].selection = changes_->item(row)->data(Qt::UserRole).toString();
        setSessionDiff();
    });
    connect(workspaceSearch_, &QLineEdit::textChanged, this, [this](const QString &text) {
        workspaceFilters_[id_].text = text;
        filterWorkspace();
    });
    connect(sessionSearch_, &QLineEdit::textChanged, this, [this](const QString &text) {
        sessionFilters_[id_].text = text;
        filterSession();
    });
    connect(tabs_, &QTabWidget::currentChanged, this, &ReviewPanel::updateActivity);
    connect(reference_, &QPushButton::clicked, this, &ReviewPanel::findReference);
    connect(export_, &QPushButton::clicked, this, [this] { buildDelivery(Delivery::Export); });
    connect(apply_, &QPushButton::clicked, this, [this] { buildDelivery(Delivery::Apply); });
    connect(reveal_, &QPushButton::clicked, this, &ReviewPanel::reveal);
    bind({});
}

void ReviewPanel::bind(const QString &id)
{
    id_ = id;
    review_ = {};
    ++refreshSequence_;
    ++fileSequence_;
    files_->clear();
    patch_->clear();
    patchPath_.clear();
    changes_->clear();
    sessionPaths_.clear();
    sessionDiff_->clear();
    {
        const QSignalBlocker blockWorkspace(workspaceSearch_);
        const QSignalBlocker blockSession(sessionSearch_);
        workspaceSearch_->setText(workspaceFilters_.value(id).text);
        sessionSearch_->setText(sessionFilters_.value(id).text);
    }
    summary_->setText(id_.isEmpty() ? "选择一个会话以查看变更。" : "点击刷新查看工作区。");
    updateActivity();
    updateActions();
    if (isVisible() && !id_.isEmpty())
        refresh();
}

void ReviewPanel::refresh()
{
    if (id_.isEmpty())
        return;
    const auto id = id_;
    const auto generation = ++refreshSequence_;
    try {
        const auto scope = controller_.scopeFor(id);
        const auto path = controller_.profileDirectory();
        const auto isolated = controller_.thread(id)->isolated;
        summary_->setText("正在读取 Git 状态…");
        runJob(this, [scope, path, id, isolated] {
            const auto active = openScope(scope.root, path, id, isolated, false);
            return readReview(active);
        }, [this, generation](const JobResult<Review> &result) {
            if (generation != refreshSequence_)
                return;
            ++fileSequence_;
            {
                const QSignalBlocker block(files_);
                files_->clear();
                review_ = result.value.value_or(Review{});
                for (const auto &change : std::as_const(review_.files)) {
                    const auto kind = change.status == "??" || change.status.contains('A') ? QString("新增") :
                                      change.status.contains('D') ? QString("删除") :
                                      change.status.contains('U') ? QString("冲突") : QString("修改");
                    auto *item = new QListWidgetItem(kind + "  " + change.path, files_);
                    item->setData(Qt::UserRole, change.path);
                    item->setToolTip(change.path);
                }
            }
            const auto branch = review_.branch.startsWith("orbite/") && review_.branch.size() > 20 ?
                                review_.branch.left(15) : review_.branch;
            summary_->setText(result.value ? QString("%1  ·  %2 个变更").arg(branch).arg(review_.files.size()) : result.error);
            summary_->setToolTip(result.value ? review_.branch : result.error);
            filterWorkspace(true);
        });
    } catch (const Failure &error) {
        summary_->setText(error.text);
    }
    updateActivity();
    updateActions();
}

void ReviewPanel::filterWorkspace(bool reload)
{
    const auto filter = workspaceFilters_.value(id_);
    const auto matches = selectFiltered(files_, filter.text.trimmed(), filter.selection);
    files_->setMaximumHeight(qBound(43, matches * 32 + 8, 175));
    const auto row = files_->currentRow();
    if (row < 0) {
        ++fileSequence_;
        patchPath_.clear();
        patch_->clear();
        patch_->setPlaceholderText(filter.text.trimmed().isEmpty() ? "没有可审阅的改动。" : "没有匹配的文件。");
    } else if (reload || patchPath_ != files_->item(row)->data(Qt::UserRole).toString()) {
        loadFile(row);
    }
    updateActions();
}

void ReviewPanel::filterSession()
{
    const auto filter = sessionFilters_.value(id_);
    selectFiltered(changes_, filter.text.trimmed(), filter.selection);
    setSessionDiff();
}

void ReviewPanel::loadFile(int row)
{
    if (row < 0 || row >= review_.files.size())
        return;
    const auto id = id_;
    const auto generation = ++fileSequence_;
    const auto change = review_.files[row];
    try {
        const auto scope = controller_.scopeFor(id);
        const auto path = controller_.profileDirectory();
        const auto isolated = controller_.thread(id)->isolated;
        if (patchPath_ != change.path) {
            patch_->clear();
            patchPath_ = change.path;
        }
        patch_->setPlaceholderText("正在读取差异…");
        runJob(this, [scope, path, id, isolated, change] {
            return readPatch(openScope(scope.root, path, id, isolated, false), change);
        }, [this, generation](const JobResult<QString> &result) {
            if (generation != fileSequence_)
                return;
            if (result.value && !result.value->isEmpty()) {
                if (patch_->toPlainText() != *result.value)
                    patch_->setPlainText(*result.value);
            } else {
                patch_->clear();
                patch_->setPlaceholderText(result.value ? "没有可显示的文本差异。" : result.error);
            }
            updateActions();
        });
    } catch (const Failure &error) {
        patch_->setPlainText(error.text);
    }
}

void ReviewPanel::updateActivity()
{
    const auto *thread = controller_.thread(id_);
    if (tabs_->currentIndex() == 1) {
        const auto paths = thread ? thread->diffs.keys() : QStringList{};
        if (sessionPaths_ != paths) {
            sessionPaths_ = paths;
            const QSignalBlocker blocked(changes_);
            changes_->clear();
            for (const auto &path : paths) {
                auto *row = new QListWidgetItem(path, changes_);
                row->setData(Qt::UserRole, path);
            }
            filterSession();
        } else {
            setSessionDiff();
        }
        return;
    }
    if (tabs_->currentIndex() != 2)
        return;

    QStringList lines;
    if (thread) {
        for (const auto &entry : thread->plan) {
            const auto item = entry.toObject();
            const auto status = item.value("status").toString();
            const auto symbol = status == "completed" ? QString("✓") : status == "in_progress" ? QString("→") : QString("·");
            lines << symbol + "  " + item.value("content").toString();
        }
        if (!lines.isEmpty())
            lines << "";
        for (const auto &message : thread->messages) {
            for (const auto &entry : message.tools) {
                const auto item = entry.toObject();
                lines << item.value("title").toString() + " · " + item.value("status").toString();
            }
        }
    }
    const auto next = lines.join('\n');
    if (activity_->toPlainText() != next)
        activity_->setPlainText(next);
    activity_->setPlaceholderText("会话开始后，这里会显示计划和工具状态。");
}

void ReviewPanel::setSessionDiff()
{
    const auto *thread = controller_.thread(id_);
    const auto *selected = changes_->currentItem();
    if (!thread || !selected) {
        sessionDiff_->clear();
        sessionDiff_->setPlaceholderText(sessionSearch_->text().trimmed().isEmpty() ?
            "会话工具返回的文件变更会出现在这里。" : "没有匹配的文件。");
        return;
    }
    const auto file = thread->diffs.value(selected->data(Qt::UserRole).toString()).toObject();
    const auto text = "变更前\n\n" + file.value("before").toString() + "\n\n变更后\n\n" + file.value("after").toString();
    if (sessionDiff_->toPlainText() != text)
        sessionDiff_->setPlainText(text);
}

void ReviewPanel::updateActions()
{
    const auto *thread = controller_.thread(id_);
    const bool isolated = thread && thread->isolated;
    const bool idle = thread && !controller_.runtime(id_).busy() && !controller_.applying() && !exporting_;
    reference_->setEnabled(files_->currentRow() >= 0 && !patch_->toPlainText().isEmpty());
    export_->setVisible(isolated);
    apply_->setVisible(isolated);
    reveal_->setVisible(isolated);
    export_->setEnabled(idle);
    apply_->setEnabled(idle);
}

void ReviewPanel::updateTheme()
{
    highlighter_->rehighlight();
}

void ReviewPanel::showActivity()
{
    tabs_->setCurrentIndex(2);
}

void ReviewPanel::findReference()
{
    const auto row = files_->currentRow();
    if (row < 0 || row >= review_.files.size())
        return;
    static const QRegularExpression hunk(R"(^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@)");
    int line = 0;
    const auto target = patch_->textCursor().block();
    for (auto block = patch_->document()->firstBlock(); block.isValid(); block = block.next()) {
        const auto text = block.text();
        const auto match = hunk.match(text);
        if (match.hasMatch()) {
            line = match.captured(1).toInt();
        } else if (block == target) {
            if (!text.startsWith('+') && !text.startsWith(' '))
                line = 0;
            break;
        } else if (line > 0 && (text.startsWith(' ') || text.startsWith('+'))) {
            ++line;
        }
        if (block == target) {
            line = 0;
            break;
        }
    }
    emit reference(review_.files[row].path, line);
}

void ReviewPanel::withScope(const std::function<void(const Scope &)> &ready)
{
    try {
        const auto scope = controller_.scopeFor(id_);
        const auto id = id_;
        const auto profile = controller_.profileDirectory();
        const auto isolated = controller_.thread(id)->isolated;
        runJob(this, [scope, id, profile, isolated] {
            return openScope(scope.root, profile, id, isolated, false);
        }, [this, id, ready](const JobResult<Scope> &result) {
            if (id != id_)
                return;
            if (result.value)
                ready(*result.value);
            else
                summary_->setText(result.error);
        });
    } catch (const Failure &error) {
        summary_->setText(error.text);
    }
}

void ReviewPanel::reveal()
{
    withScope([](const Scope &scope) { QDesktopServices::openUrl(QUrl::fromLocalFile(scope.cwd)); });
}

void ReviewPanel::buildDelivery(Delivery action)
{
    const auto id = id_;
    try {
        const auto raw = controller_.scopeFor(id, true);
        const auto profile = controller_.profileDirectory();
        exporting_ = true;
        updateActions();
        summary_->setText("正在生成完整补丁…");
        runJob(this, [raw, profile, id, action] {
            auto patch = buildPatch(openScope(raw.root, profile, id, true, false));
            if (action == Delivery::Apply && !patch.data.isEmpty())
                checkApply(raw.root, patch);
            return patch;
        }, [this, id, action, destination = raw.root](const JobResult<Patch> &result) {
            exporting_ = false;
            updateActions();
            if (id != id_)
                return;
            if (!result.value) {
                summary_->setText(result.error);
                return;
            }
            const auto &patch = *result.value;
            if (patch.data.isEmpty()) {
                summary_->setText("工作树没有可交付的改动。");
                return;
            }
            if (action == Delivery::Apply) {
                QMessageBox dialog(QMessageBox::Question, "应用工作树变更",
                                   QString("将 %1 个文件的改动应用到\n%2").arg(patch.paths.size()).arg(destination),
                                   QMessageBox::Apply | QMessageBox::Cancel, this);
                dialog.setTextFormat(Qt::PlainText);
                dialog.setInformativeText("改动写入项目工作区，保留由你审阅和提交。");
                dialog.setDetailedText(patch.paths.join('\n'));
                dialog.setDefaultButton(QMessageBox::Cancel);
                if (dialog.exec() == QMessageBox::Apply) {
                    try {
                        controller_.deliver(id, patch);
                        summary_->setText("已提交应用请求。");
                    } catch (const Failure &error) {
                        summary_->setText(error.text);
                    }
                } else {
                    refresh();
                }
                return;
            }
            const auto path = QFileDialog::getSaveFileName(this, "导出补丁", "Orbite-" + id.left(8) + ".patch",
                                                          "Git 补丁 (*.patch)");
            if (path.isEmpty()) {
                refresh();
                return;
            }
            QSaveFile out(path);
            if (!out.open(QIODevice::WriteOnly) || out.write(patch.data) != patch.data.size() || !out.commit()) {
                summary_->setText("未能写入补丁文件。");
                return;
            }
            summary_->setText(QString("已导出 %1 个文件的改动。").arg(patch.paths.size()));
        });
    } catch (const Failure &error) {
        summary_->setText(error.text);
    }
}

}
