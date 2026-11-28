#include "window.h"
#include "computer_use.h"
#include "jobs.h"
#include "review_panel.h"
#include "theme.h"
#include "workbench.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QIcon>
#include <QPixmap>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollBar>
#include <QShortcut>
#include <QSlider>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringListModel>
#include <QStyle>
#include <QStyleHints>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QToolButton>
#include <QTransform>
#include <QTreeWidget>
#include <QUrl>
#include <QVariantAnimation>
#include <QWidgetAction>

namespace orbite {
namespace {

// Render two bounded snapshots, not two live widget hierarchies per frame.
class PageFlip : public QWidget {
public:
    explicit PageFlip(QWidget *parent) : QWidget(parent)
    {
        setObjectName("pageFlip");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setFocusPolicy(Qt::NoFocus);
        timeline_.setStartValue(0.0);
        timeline_.setEndValue(1.0);
        timeline_.setDuration(180);
        timeline_.setEasingCurve(QEasingCurve::InOutCubic);
        connect(&timeline_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            progress_ = value.toReal();
            update();
        });
        connect(&timeline_, &QVariantAnimation::finished, this, &PageFlip::cancel);
        hide();
    }

    void cancel()
    {
        timeline_.stop();
        hide();
        from_ = {};
        to_ = {};
    }

    void start(QPixmap from, QPixmap to, bool forward)
    {
        if (from.isNull() || to.isNull() || from.size() != to.size() ||
            from.devicePixelRatio() != to.devicePixelRatio())
            return;
        from_ = std::move(from);
        to_ = std::move(to);
        direction_ = forward ? 1.0 : -1.0;
        progress_ = 0.0;
        setGeometry(parentWidget()->rect());
        raise();
        show();
        timeline_.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter paint(this);
        paint.fillRect(rect(), palette().window());
        const bool incoming = progress_ >= 0.5;
        const auto &face = incoming ? to_ : from_;
        // Switch faces at the edge of the same rotating plane, with no mirrored text.
        const qreal angle = direction_ * (progress_ - (incoming ? 1.0 : 0.0)) * 180.0;
        if (face.isNull() || qAbs(angle) >= 89.7)
            return;
        const QPointF center(width() * 0.5, height() * 0.5);
        QTransform projection;
        projection.translate(center.x(), center.y());
        projection.rotate(angle, Qt::YAxis, qMax(1, width() * 2));
        projection.translate(-center.x(), -center.y());
        paint.setTransform(projection);
        paint.setRenderHint(QPainter::SmoothPixmapTransform);
        paint.drawPixmap(QPoint(0, 0), face);
    }

private:
    QVariantAnimation timeline_;
    QPixmap from_;
    QPixmap to_;
    qreal direction_ = 1.0;
    qreal progress_ = 0.0;
};

class PageStack : public QStackedWidget {
public:
    explicit PageStack(QWidget *parent) : QStackedWidget(parent), flip_(new PageFlip(this)) {}

    void cancelFlip() { flip_->cancel(); }

    bool wantsFlip(int index) const
    {
        const auto dpr = devicePixelRatioF();
        return currentIndex() != index && isVisible() && !window()->isMinimized() &&
            width() >= 240 && height() >= 200 && width() * qreal(height()) * dpr * dpr <= 5000000 &&
            style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, this) > 0;
    }

    void startFlip(QPixmap from, QPixmap to, bool forward)
    {
        flip_->start(std::move(from), std::move(to), forward);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        cancelFlip();
        QStackedWidget::resizeEvent(event);
    }

private:
    PageFlip *flip_;
};

QStringList localAttachmentPaths(const QMimeData *data)
{
    QStringList paths;
    for (const auto &url : data->urls()) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    return paths;
}

bool acceptAttachmentCopy(QDropEvent *event)
{
    if (!event->possibleActions().testFlag(Qt::CopyAction)) {
        event->ignore();
        return false;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    return true;
}

class ComposerFrame : public QFrame {
public:
    using QFrame::QFrame;
    std::function<void(const QStringList &)> dropFiles;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override { dragMoveEvent(event); }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (dropFiles && !localAttachmentPaths(event->mimeData()).isEmpty())
            acceptAttachmentCopy(event);
        else
            event->ignore();
    }

    void dropEvent(QDropEvent *event) override
    {
        const auto paths = localAttachmentPaths(event->mimeData());
        if (!dropFiles || paths.isEmpty()) {
            event->ignore();
            return;
        }
        if (acceptAttachmentCopy(event))
            dropFiles(paths);
    }
};

class ComposerEdit : public QPlainTextEdit {
public:
    explicit ComposerEdit(QWidget *parent)
        : QPlainTextEdit(parent), commands_(new QStringListModel(this)), completer_(new QCompleter(commands_, this))
    {
        completer_->setWidget(this);
        completer_->setCaseSensitivity(Qt::CaseInsensitive);
        completer_->setMaxVisibleItems(7);
        connect(this, &QPlainTextEdit::textChanged, this, &ComposerEdit::updateSuggestions);
        connect(completer_, qOverload<const QString &>(&QCompleter::activated), this, [this](const QString &command) {
            setPlainText(command + " ");
            moveCursor(QTextCursor::End);
        });
    }

    void setCommands(const QList<QPair<QString, QString>> &commands)
    {
        QStringList names;
        for (const auto &[name, detail] : commands)
            names << "/" + name;
        if (commands_->stringList() != names) {
            completer_->popup()->hide();
            commands_->setStringList(names);
        }
    }

    std::function<void(const QStringList &, const QImage &)> attachments;
    std::function<void(bool)> focusChanged;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        const auto key = event->key();
        if (completer_->popup()->isVisible() &&
            (key == Qt::Key_Enter || key == Qt::Key_Return || key == Qt::Key_Escape ||
             key == Qt::Key_Tab || key == Qt::Key_Backtab)) {
            event->ignore();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

    void focusInEvent(QFocusEvent *event) override
    {
        QPlainTextEdit::focusInEvent(event);
        if (focusChanged)
            focusChanged(true);
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        QPlainTextEdit::focusOutEvent(event);
        if (focusChanged)
            focusChanged(false);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (attachments && !localAttachmentPaths(event->mimeData()).isEmpty())
            acceptAttachmentCopy(event);
        else
            QPlainTextEdit::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (attachments && !localAttachmentPaths(event->mimeData()).isEmpty())
            acceptAttachmentCopy(event);
        else
            QPlainTextEdit::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        const auto paths = localAttachmentPaths(event->mimeData());
        if (attachments && !paths.isEmpty()) {
            if (acceptAttachmentCopy(event))
                attachments(paths, {});
        } else {
            QPlainTextEdit::dropEvent(event);
        }
    }

    bool canInsertFromMimeData(const QMimeData *data) const override
    {
        return data->hasUrls() || data->hasImage() || QPlainTextEdit::canInsertFromMimeData(data);
    }

    void insertFromMimeData(const QMimeData *data) override
    {
        const auto paths = localAttachmentPaths(data);
        const auto image = paths.isEmpty() ? qvariant_cast<QImage>(data->imageData()) : QImage();
        if ((!paths.isEmpty() || !image.isNull()) && attachments)
            attachments(paths, image);
        else
            QPlainTextEdit::insertFromMimeData(data);
    }

private:
    void updateSuggestions()
    {
        static const QRegularExpression slash(R"(^/[A-Za-z0-9_-]*$)");
        const QString text = toPlainText();
        if (!hasFocus() || commands_->rowCount() == 0 || !slash.match(text).hasMatch()) {
            completer_->popup()->hide();
            return;
        }
        completer_->setCompletionPrefix(text);
        if (!completer_->completionCount()) {
            completer_->popup()->hide();
            return;
        }
        auto area = cursorRect();
        area.setWidth(230);
        completer_->complete(area);
        completer_->popup()->setCurrentIndex(completer_->completionModel()->index(0, 0));
    }

    QStringListModel *commands_;
    QCompleter *completer_;

};

class Transcript : public QTextBrowser {
public:
    using QTextBrowser::QTextBrowser;

protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};

QPushButton *button(const QString &title, QWidget *parent, bool primary = false)
{
    auto *result = new QPushButton(title, parent);
    if (primary)
        result->setProperty("primary", true);
    return result;
}

QLabel *label(const QString &text, const QString &name, QWidget *parent)
{
    auto *result = new QLabel(text, parent);
    result->setObjectName(name);
    result->setTextFormat(Qt::PlainText);
    return result;
}

QString projectName(const Project &project)
{
    const QString name = QFileInfo(project.root).fileName();
    return name.isEmpty() ? project.root : name;
}

QString sessionSuffix(const Thread &thread, const Runtime &run)
{
    if (run.client && !run.client->permissions().isEmpty())
        return "待审批";
    if (run.phase == Phase::Running)
        return "运行中";
    if (run.phase == Phase::NeedAuth)
        return "待登录";
    if (run.phase == Phase::Preparing || run.phase == Phase::Connecting)
        return "准备中";
    if (thread.interrupted)
        return "已中断";
    return {};
}

QString readableToolState(const QString &status)
{
    if (status == "completed")
        return "完成";
    if (status == "in_progress")
        return "执行中";
    if (status == "failed")
        return "失败";
    if (status == "pending")
        return "等待";
    return status;
}

}

Window::Window(Controller &controller)
    : controller_(controller)
{
    setWindowTitle("Orbite");
    setWindowIcon(QIcon(":/assets/orbite-mark.png"));
    resize(1240, 800);
    setMinimumSize(850, 610);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    setCentralWidget(splitter);

    auto *rail = new QWidget(splitter);
    rail->setObjectName("rail");
    rail->setMinimumWidth(195);
    rail->setMaximumWidth(340);
    auto *side = new QVBoxLayout(rail);
    side->setContentsMargins(12, 15, 12, 11);
    side->setSpacing(6);
    auto *brand = new QHBoxLayout;
    auto *mark = new QLabel(rail);
    mark->setPixmap(QPixmap(":/assets/orbite-mark.png").scaled(25, 25, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brand->addWidget(mark);
    brand->addWidget(label("Orbite", "brand", rail), 1);
    side->addLayout(brand);
    side->addSpacing(8);
    homeButton_ = button("工作台", rail);
    homeButton_->setObjectName("workbenchButton");
    homeButton_->setCheckable(true);
    auto *sideNew = button("新会话", rail);
    for (auto *entry : {homeButton_, sideNew}) {
        entry->setProperty("ghost", true);
        entry->setProperty("railNav", true);
    }
    setLineIcon(homeButton_, Icon::Home);
    setLineIcon(sideNew, Icon::Compose);
    side->addWidget(homeButton_);
    side->addWidget(sideNew);
    search_ = new QLineEdit(rail);
    search_->setObjectName("projectSearch");
    search_->setPlaceholderText("搜索项目和会话");
    search_->setClearButtonEnabled(true);
    side->addWidget(search_);
    side->addSpacing(8);
    auto *projectRow = new QHBoxLayout;
    projectRow->addWidget(label("项目", "railLabel", rail), 1);
    auto *open = button("", rail);
    open->setProperty("ghost", true);
    open->setObjectName("openProject");
    open->setAccessibleName("打开项目");
    open->setToolTip("打开项目…");
    open->setFixedWidth(28);
    setLineIcon(open, Icon::Plus);
    projectRow->addWidget(open);
    side->addLayout(projectRow);
    nav_ = new QTreeWidget(rail);
    nav_->setObjectName("navigation");
    nav_->setColumnCount(2);
    nav_->setHeaderHidden(true);
    nav_->header()->setStretchLastSection(false);
    nav_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    nav_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    nav_->setIndentation(15);
    nav_->setTextElideMode(Qt::ElideRight);
    nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setContextMenuPolicy(Qt::CustomContextMenu);
    side->addWidget(nav_, 1);
    archiveFilter_ = new QCheckBox("已归档会话", rail);
    archiveFilter_->setObjectName("archiveFilter");
    side->addWidget(archiveFilter_);
    auto *railRule = new QFrame(rail);
    railRule->setObjectName("railRule");
    railRule->setFixedHeight(1);
    side->addWidget(railRule);
    auto *railActions = new QHBoxLayout;
    auto *settings = button("设置", rail);
    auto *extensions = button("扩展", rail);
    settings->setProperty("ghost", true);
    extensions->setProperty("ghost", true);
    setLineIcon(settings, Icon::Settings);
    setLineIcon(extensions, Icon::Extension);
    railActions->addWidget(settings);
    railActions->addWidget(extensions);
    side->addLayout(railActions);

    auto *conversation = new QWidget(splitter);
    conversation->setObjectName("conversation");
    conversation->setMinimumWidth(420);
    auto *main = new QVBoxLayout(conversation);
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(0);

    auto *header = new QWidget(conversation);
    header->setObjectName("projectHeader");
    auto *heading = new QVBoxLayout(header);
    heading->setContentsMargins(21, 10, 18, 9);
    heading->setSpacing(2);
    auto *breadcrumbs = new QHBoxLayout;
    projectLabel_ = new ElidedLabel("Orbite  /  工作台", header);
    projectLabel_->setObjectName("caption");
    status_ = label("", "engineStatus", header);
    breadcrumbs->addWidget(projectLabel_, 1);
    breadcrumbs->addWidget(status_);
    heading->addLayout(breadcrumbs);
    auto *headingRow = new QHBoxLayout;
    title_ = new ElidedLabel("", header);
    title_->setObjectName("threadTitle");
    newButton_ = button("新会话", header);
    newButton_->setProperty("ghost", true);
    setLineIcon(newButton_, Icon::Compose);
    moreButton_ = button("⋯", header);
    moreButton_->setProperty("ghost", true);
    moreButton_->setAccessibleName("会话菜单");
    reviewButton_ = button("审阅", header);
    reviewButton_->setCheckable(true);
    reviewButton_->setObjectName("reviewToggle");
    setLineIcon(reviewButton_, Icon::Review);
    headingRow->addWidget(title_, 1);
    headingRow->addWidget(newButton_);
    headingRow->addWidget(reviewButton_);
    headingRow->addWidget(moreButton_);
    heading->addLayout(headingRow);
    main->addWidget(header);

    pages_ = new PageStack(conversation);
    home_ = new Workbench(controller_, pages_);
    pages_->addWidget(home_);

    auto *work = new QWidget(pages_);
    auto *workShell = new QHBoxLayout(work);
    workShell->setContentsMargins(23, 0, 23, 15);
    workShell->setSpacing(0);
    auto *lane = new QWidget(work);
    lane->setMaximumWidth(800);
    workShell->addStretch(1);
    workShell->addWidget(lane, 100);
    workShell->addStretch(1);
    auto *workLayout = new QVBoxLayout(lane);
    workLayout->setContentsMargins(0, 0, 0, 0);
    workLayout->setSpacing(9);
    welcome_ = new QWidget(lane);
    auto *welcomeLayout = new QVBoxLayout(welcome_);
    welcomeLayout->setContentsMargins(0, 0, 0, 13);
    welcomeLayout->setSpacing(9);
    welcomeLayout->addStretch();
    auto *orbital = new QLabel(welcome_);
    orbital->setPixmap(QPixmap(":/assets/orbite-mark.png").scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    welcomeLayout->addWidget(orbital, 0, Qt::AlignHCenter);
    welcomeLayout->addWidget(label("从一个任务开始", "sessionWelcomeTitle", welcome_), 0, Qt::AlignHCenter);
    welcomeLayout->addWidget(label("描述要做的事，或把项目资料拖进输入框。", "sessionWelcomeText", welcome_),
                             0, Qt::AlignHCenter);
    workLayout->addWidget(welcome_, 3);

    transcript_ = new Transcript(work);
    transcript_->setObjectName("transcript");
    transcript_->setOpenLinks(false);
    transcript_->setOpenExternalLinks(false);
    auto baseFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    baseFont.setPointSizeF(12.5);
    transcript_->document()->setDefaultFont(baseFont);
    transcript_->document()->setDocumentMargin(7);
    workLayout->addWidget(transcript_, 1);

    approvalCard_ = new QFrame(work);
    approvalCard_->setObjectName("approvalCard");
    auto *permission = new QVBoxLayout(approvalCard_);
    permission->setContentsMargins(9, 7, 9, 8);
    auto *permissionHead = new QHBoxLayout;
    permissionHead->addWidget(label("需要审批", "caption", approvalCard_));
    approvalChoice_ = new QComboBox(approvalCard_);
    permissionHead->addWidget(approvalChoice_, 1);
    reject_ = button("拒绝", approvalCard_);
    allow_ = button("允许一次", approvalCard_, true);
    allow_->setObjectName("allowOnce");
    permissionHead->addWidget(reject_);
    permissionHead->addWidget(allow_);
    permission->addLayout(permissionHead);
    approvalBody_ = new QPlainTextEdit(approvalCard_);
    approvalBody_->setObjectName("permissionBody");
    approvalBody_->setReadOnly(true);
    approvalBody_->setFixedHeight(90);
    permission->addWidget(approvalBody_);
    workLayout->addWidget(approvalCard_);

    queuedCard_ = new QFrame(work);
    queuedCard_->setObjectName("queueCard");
    auto *queued = new QHBoxLayout(queuedCard_);
    queued->setContentsMargins(9, 5, 9, 5);
    queuedText_ = label("", "status", queuedCard_);
    queuedText_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    runQueued_ = button("运行排队消息", queuedCard_);
    restoreQueued_ = button("取回草稿", queuedCard_);
    queued->addWidget(queuedText_, 1);
    queued->addWidget(restoreQueued_);
    queued->addWidget(runQueued_);
    workLayout->addWidget(queuedCard_);

    auto *composerFrame = new ComposerFrame(work);
    composerFrame->dropFiles = [this](const auto &paths) { appendFiles(paths, {}); };
    composer_ = composerFrame;
    composer_->setObjectName("composerCard");
    auto *composerLayout = new QVBoxLayout(composer_);
    composerLayout->setContentsMargins(10, 9, 10, 9);
    composerLayout->setSpacing(6);
    files_ = new QWidget(composer_);
    auto *fileGrid = new QGridLayout(files_);
    fileGrid->setContentsMargins(3, 0, 3, 0);
    fileGrid->setSpacing(4);
    composerLayout->addWidget(files_);
    auto *edit = new ComposerEdit(composer_);
    edit->setObjectName("composerEdit");
    edit->setFixedHeight(73);
    edit->setPlaceholderText("描述你的任务，或拖入文件和文件夹…");
    edit->focusChanged = [this](bool focused) {
        composer_->setProperty("focused", focused);
        composer_->style()->unpolish(composer_);
        composer_->style()->polish(composer_);
        composer_->update();
    };
    edit->attachments = [this](const auto &paths, const auto &image) { appendFiles(paths, image); };
    input_ = edit;
    composerLayout->addWidget(input_);
    auto *composerRow = new QHBoxLayout;
    composerRow->setSpacing(5);
    add_ = button("", composer_);
    add_->setProperty("ghost", true);
    setLineIcon(add_, Icon::Plus);
    add_->setFixedWidth(31);
    add_->setAccessibleName("添加附件");
    add_->setToolTip("添加文件、文件夹或剪贴板截图");
    auto *attachments = new QMenu(add_);
    attachments->addAction("添加文件…", this, [this] { chooseAttachment(false); });
    attachments->addAction("添加文件夹…", this, [this] { chooseAttachment(true); });
    attachments->addAction("粘贴截图或文件", input_, &QPlainTextEdit::paste);
    add_->setMenu(attachments);
    isolated_ = button("项目目录", composer_);
    isolated_->setCheckable(true);
    isolated_->setProperty("pill", true);
    isolated_->setToolTip("从项目当前提交创建独立 Git 工作树");
    setLineIcon(isolated_, Icon::Folder);
    commands_ = new QToolButton(composer_);
    commands_->setText("/");
    commands_->setAccessibleName("斜杠命令");
    commands_->setToolTip("斜杠命令");
    commands_->setProperty("ghost", true);
    commands_->setProperty("pill", true);
    commands_->setPopupMode(QToolButton::InstantPopup);
    commandMenu_ = new QMenu(commands_);
    commands_->setMenu(commandMenu_);
    model_ = new QToolButton(composer_);
    model_->setText("模型");
    model_->setProperty("pill", true);
    model_->setProperty("ghost", true);
    model_->setMaximumWidth(120);
    model_->setPopupMode(QToolButton::InstantPopup);
    modelMenu_ = new QMenu(model_);
    model_->setMenu(modelMenu_);
    effort_ = new QToolButton(composer_);
    effort_->setText("思考");
    effort_->setObjectName("effortControl");
    effort_->setPopupMode(QToolButton::InstantPopup);
    effort_->setProperty("ghost", true);
    effort_->setProperty("pill", true);
    effortMenu_ = new QMenu(effort_);
    effort_->setMenu(effortMenu_);
    effort_->hide();
    connect_ = button("连接", composer_);
    stop_ = button("", composer_);
    stop_->setProperty("ghost", true);
    stop_->setAccessibleName("停止");
    stop_->setToolTip("停止运行");
    stop_->setFixedWidth(30);
    setLineIcon(stop_, Icon::Stop);
    queue_ = button("排队", composer_);
    send_ = button("", composer_, true);
    send_->setObjectName("sendPrompt");
    send_->setAccessibleName("发送消息");
    send_->setToolTip("发送 · " + QKeySequence(Qt::CTRL | Qt::Key_Return).toString(QKeySequence::NativeText));
    composerRow->addWidget(add_);
    composerRow->addWidget(isolated_);
    composerRow->addWidget(model_);
    composerRow->addWidget(effort_);
    composerRow->addWidget(commands_);
    composerRow->addStretch();
    composerRow->addWidget(connect_);
    composerRow->addWidget(queue_);
    composerRow->addWidget(stop_);
    composerRow->addWidget(send_);
    composerLayout->addLayout(composerRow);
    workLayout->addWidget(composer_);
    auto *footnote = label(QKeySequence(Qt::CTRL | Qt::Key_Return).toString(QKeySequence::NativeText) + " 发送",
                           "composerFootnote", lane);
    workLayout->addWidget(footnote, 0, Qt::AlignHCenter);
    welcomeSpace_ = new QWidget(lane);
    workLayout->addWidget(welcomeSpace_, 4);
    pages_->addWidget(work);
    main->addWidget(pages_, 1);

    review_ = new ReviewPanel(controller_, splitter);
    splitter->addWidget(rail);
    splitter->addWidget(conversation);
    splitter->addWidget(review_);
    review_->hide();
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 10);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({222, 650, 372});

    auto *fileMenu = menuBar()->addMenu("项目");
    fileMenu->addAction("打开项目…", QKeySequence::Open, this, &Window::chooseProject);
    fileMenu->addAction("新会话", QKeySequence::New, this, &Window::createSession);
    fileMenu->addAction("导入 Antigravity 会话…", this, [this] {
        if (project_.isEmpty())
            return info("请先选择项目。");
        info("正在读取该项目的 Antigravity 会话…");
        controller_.listRemote(project_);
    });
    fileMenu->addAction("导入 Orbite 0.7 资料…", this, &Window::importLegacy);
    auto *viewMenu = menuBar()->addMenu("视图");
    viewMenu->addAction("工作台", QKeySequence(Qt::CTRL | Qt::Key_1), this, &Window::showHome);
    viewMenu->addAction("搜索会话", QKeySequence(Qt::CTRL | Qt::Key_K), search_, qOverload<>(&QLineEdit::setFocus));
    viewMenu->addAction("切换审阅", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this,
                        [this] { displayReview(review_->isHidden()); });
    auto *reduceMotion = viewMenu->addAction("减少动态效果");
    reduceMotion->setObjectName("reduceMotion");
    reduceMotion->setCheckable(true);
    reduceMotion->setChecked(controller_.setting("reduceMotion") == "true");
    connect(reduceMotion, &QAction::toggled, this, [this](bool reduced) {
        static_cast<PageStack *>(pages_)->cancelFlip();
        action([this, reduced] { controller_.setSetting("reduceMotion", reduced ? "true" : "false"); });
    });
    viewMenu->addAction("设置…", QKeySequence::Preferences, this, &Window::showSettings);
    menuBar()->addMenu("扩展")->addAction("Computer Use…", this, &Window::showExtension);

    transcriptTimer_.setSingleShot(true);
    transcriptTimer_.setInterval(64);
    connect(&transcriptTimer_, &QTimer::timeout, this, &Window::renderTranscript);
    connect(open, &QPushButton::clicked, this, &Window::chooseProject);
    connect(homeButton_, &QPushButton::clicked, this, &Window::showHome);
    connect(sideNew, &QPushButton::clicked, this, &Window::createSession);
    connect(newButton_, &QPushButton::clicked, this, &Window::createSession);
    connect(home_, &Workbench::openProject, this, &Window::chooseProject);
    connect(home_, &Workbench::threadSelected, this, [this](const QString &id) {
        selectThread(id);
        rebuildNavigation();
    });
    connect(home_, &Workbench::projectSelected, this, [this](const QString &id) {
        project_ = id;
        selectThread({});
        rebuildNavigation();
    });
    connect(moreButton_, &QPushButton::clicked, this, [this] {
        threadMenu(moreButton_->mapToGlobal(QPoint(0, moreButton_->height())), current_);
    });
    connect(reviewButton_, &QPushButton::toggled, this, &Window::displayReview);
    connect(review_, &ReviewPanel::dismissed, this, [this] { displayReview(false); });
    connect(review_, &ReviewPanel::reference, this, [this](const QString &path, int line) {
        auto location = path;
        if (line > 0)
            location += ":" + QString::number(line);
        if (input_->isEnabled()) {
            input_->moveCursor(QTextCursor::End);
            input_->insertPlainText((input_->toPlainText().isEmpty() ? QString() : QString("\n")) +
                                    QString("请查看“%1”：").arg(location));
            input_->setFocus();
        }
    });
    connect(settings, &QPushButton::clicked, this, &Window::showSettings);
    connect(extensions, &QPushButton::clicked, this, &Window::showExtension);
    connect(search_, &QLineEdit::textChanged, this, &Window::rebuildNavigation);
    connect(archiveFilter_, &QCheckBox::toggled, this, &Window::rebuildNavigation);
    connect(nav_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (item) {
            project_ = item->data(0, Qt::UserRole).toString();
            selectThread(item->data(0, Qt::UserRole + 1).toString());
        }
    });
    connect(nav_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto *item = nav_->itemAt(point);
        if (item) {
            nav_->setCurrentItem(item);
            threadMenu(nav_->viewport()->mapToGlobal(point), item->data(0, Qt::UserRole + 1).toString());
        }
    });
    connect(input_, &QPlainTextEdit::textChanged, this, [this] {
        if (const auto *thread = controller_.thread(current_))
            controller_.setDraft(current_, {input_->toPlainText(), thread->draft.files});
    });
    connect(isolated_, &QPushButton::toggled, this, [this](bool enabled) { controller_.setIsolated(current_, enabled); });
    connect(connect_, &QPushButton::clicked, this, [this] {
        if (controller_.runtime(current_).phase == Phase::NeedAuth)
            controller_.authenticate(current_);
        else
            controller_.connectThread(current_);
    });
    connect(send_, &QPushButton::clicked, this, [this] {
        if (controller_.runtime(current_).phase == Phase::Running)
            controller_.steer(current_);
        else
            controller_.send(current_);
    });
    connect(stop_, &QPushButton::clicked, this, [this] { controller_.stop(current_); });
    connect(queue_, &QPushButton::clicked, this, [this] { controller_.queueDraft(current_); });
    connect(runQueued_, &QPushButton::clicked, this, [this] { controller_.send(current_, SendSource::Queue); });
    connect(restoreQueued_, &QPushButton::clicked, this, [this] { action([this] { controller_.restoreQueue(current_); }); });
    auto *submit = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), input_);
    connect(submit, &QShortcut::activated, send_, &QPushButton::click);
    connect(modelMenu_, &QMenu::aboutToShow, this, &Window::buildModelMenu);
    connect(effortMenu_, &QMenu::aboutToShow, this, &Window::buildEffortMenu);
    connect(transcript_, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url == QUrl("orbite:activity")) {
            displayReview(true);
            review_->showActivity();
        } else if (url == QUrl("orbite:older")) {
            transcriptLimit_ += 120;
            renderTranscript();
            transcript_->verticalScrollBar()->setValue(0);
        }
    });
    connect(transcript_, &QTextBrowser::selectionChanged, this, [this] {
        if (!transcript_->textCursor().hasSelection() && transcriptDeferred_) {
            transcriptDeferred_ = false;
            scheduleTranscript();
        }
    });
    connect(commandMenu_, &QMenu::aboutToShow, this, &Window::buildCommandMenu);
    connect(approvalChoice_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0)
            return;
        const auto approval = shownPermissions_.value(approvalChoice_->currentData().toString());
        approvalBody_->setPlainText(approval.detail);
        allow_->setEnabled(!approval.allow.isEmpty());
    });
    connect(allow_, &QPushButton::clicked, this,
            [this] { controller_.decide(current_, approvalChoice_->currentData().toString(), true); });
    connect(reject_, &QPushButton::clicked, this,
            [this] { controller_.decide(current_, approvalChoice_->currentData().toString(), false); });

    connect(&controller_, &Controller::navigationChanged, this, &Window::rebuildNavigation);
    connect(&controller_, &Controller::threadChanged, this, [this](const QString &id) {
        if (id == current_)
            syncThread();
    });
    connect(&controller_, &Controller::transcriptChanged, this, [this](const QString &id) {
        if (id == current_)
            scheduleTranscript();
    });
    connect(&controller_, &Controller::statusMessage, this, &Window::info);
    connect(&controller_, &Controller::remoteSessions, this, &Window::showRemote);
    connect(&controller_, &Controller::remoteFailed, this, &Window::info);
    connect(&controller_, &Controller::remoteAuthentication, this, [this](bool) {
        QMessageBox prompt(QMessageBox::Question, "连接 Antigravity", "读取会话历史需要 Google 登录。",
                           QMessageBox::Ok | QMessageBox::Cancel, this);
        prompt.setDefaultButton(QMessageBox::Cancel);
        if (prompt.exec() == QMessageBox::Ok)
            controller_.authenticateRemote();
        else
            controller_.cancelRemote();
    });
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        applyAppearance(controller_.setting("theme"));
    });

    const auto selected = controller_.setting("selected");
    if (const auto *thread = controller_.thread(selected)) {
        if (thread->archived)
            archiveFilter_->setChecked(true);
        selectThread(selected);
    } else {
        syncThread();
    }
    rebuildNavigation();
}

void Window::info(const QString &message)
{
    if (!message.isEmpty())
        statusBar()->showMessage(message, 12000);
}

void Window::action(const std::function<void()> &execute)
{
    try {
        execute();
    } catch (const Failure &error) {
        info(error.text);
    }
}

void Window::chooseProject()
{
    const auto suggested = controller_.projects().value(project_).root;
    const auto path = QFileDialog::getExistingDirectory(this, "打开项目目录", suggested);
    if (!path.isEmpty())
        openPath(path);
}

void Window::openPath(const QString &path)
{
    action([this, path] {
        project_ = controller_.openProject(path);
        const Thread *recent = nullptr;
        for (const auto &thread : controller_.threads()) {
            if (thread.project == project_ && !thread.archived && (!recent || thread.updated > recent->updated))
                recent = &thread;
        }
        if (recent) {
            selectThread(recent->id);
        } else {
            selectThread(controller_.newThread(project_));
            input_->setFocus();
        }
        rebuildNavigation();
    });
}

void Window::createSession()
{
    if (project_.isEmpty()) {
        chooseProject();
        return;
    }
    action([this] {
        archiveFilter_->setChecked(false);
        selectThread(controller_.newThread(project_));
        rebuildNavigation();
        input_->setFocus();
    });
}

void Window::showHome()
{
    project_.clear();
    selectThread({});
    rebuildNavigation();
}

void Window::selectThread(const QString &id)
{
    auto *stack = static_cast<PageStack *>(pages_);
    const int nextPage = controller_.thread(id) ? 1 : 0;
    QElapsedTimer capture;
    QPixmap previous;
    if (id != current_ || stack->currentIndex() != nextPage)
        stack->cancelFlip();
    if (stack->wantsFlip(nextPage) && controller_.setting("reduceMotion") != "true") {
        capture.start();
        previous = stack->grab();
    }
    current_ = id;
    if (const auto *thread = controller_.thread(id))
        project_ = thread->project;
    action([this, id] { controller_.setSetting("selected", id); });
    syncThread();
    scheduleTranscript();
    if (!previous.isNull() && capture.elapsed() < 42 && stack->currentIndex() == nextPage) {
        if (nextPage == 1 && transcript_->isVisible()) {
            transcriptTimer_.stop();
            renderTranscript();
        }
        if (capture.elapsed() < 42) {
            auto next = stack->grab();
            if (capture.elapsed() < 42)
                stack->startFlip(std::move(previous), std::move(next), nextPage == 1);
        }
    }
}

void Window::rebuildNavigation()
{
    const QSignalBlocker guard(nav_);
    QSet<QString> folded;
    for (int index = 0; index < nav_->topLevelItemCount(); ++index) {
        auto *item = nav_->topLevelItem(index);
        if (!item->isExpanded())
            folded.insert(item->data(0, Qt::UserRole).toString());
    }
    nav_->clear();
    auto ordered = controller_.projects().values();
    std::sort(ordered.begin(), ordered.end(), [](const Project &a, const Project &b) {
        return projectName(a).localeAwareCompare(projectName(b)) < 0;
    });
    const auto query = search_->text().trimmed();
    QTreeWidgetItem *selection = nullptr;
    for (const auto &project : ordered) {
        QList<const Thread *> threads;
        const auto name = projectName(project);
        const auto byProject = project.root.contains(query, Qt::CaseInsensitive);
        for (const auto &thread : controller_.threads()) {
            if (thread.project != project.id || thread.archived != archiveFilter_->isChecked())
                continue;
            bool match = byProject || thread.title.contains(query, Qt::CaseInsensitive);
            if (!match) {
                match = std::any_of(thread.messages.cbegin(), thread.messages.cend(), [&](const Message &message) {
                    return message.text.contains(query, Qt::CaseInsensitive);
                });
            }
            if (match)
                threads << &thread;
        }
        if (threads.isEmpty() && !byProject)
            continue;
        std::sort(threads.begin(), threads.end(), [](const auto *a, const auto *b) {
            return a->pinned != b->pinned ? a->pinned : a->updated > b->updated;
        });
        auto *root = new QTreeWidgetItem(nav_, {name});
        root->setData(0, Qt::UserRole, project.id);
        root->setToolTip(0, project.root);
        root->setIcon(0, lineIcon(Icon::Folder));
        auto font = root->font(0);
        font.setBold(true);
        root->setFont(0, font);
        if (project.id == project_ && current_.isEmpty())
            selection = root;
        for (const auto *thread : threads) {
            const auto text = (thread->pinned ? QString("★  ") : QString()) + thread->title;
            const auto status = sessionSuffix(*thread, controller_.runtime(thread->id));
            auto *item = new QTreeWidgetItem(root, {text, status});
            item->setForeground(1, colors().accent);
            item->setTextAlignment(1, Qt::AlignVCenter | Qt::AlignRight);
            item->setData(0, Qt::UserRole, project.id);
            item->setData(0, Qt::UserRole + 1, thread->id);
            item->setToolTip(0, text);
            if (thread->id == current_)
                selection = item;
        }
        root->setExpanded(!folded.contains(project.id) || project.id == project_ || !query.isEmpty());
    }
    if (selection)
        nav_->setCurrentItem(selection);
    if (current_.isEmpty())
        home_->rebuild(project_);
}

void Window::syncThread()
{
    const auto *thread = controller_.thread(current_);
    const auto project = controller_.projects().value(project_);
    const auto name = projectName(project);
    const auto run = controller_.runtime(current_);
    projectLabel_->setText(project_.isEmpty() ? "Orbite  /  工作台" :
                           "Orbite  /  " + name + (thread && thread->isolated ? "  /  独立工作树" : ""));
    projectLabel_->setToolTip(project.root);
    title_->setText(thread ? thread->title : QString());
    title_->setToolTip(title_->text());
    const bool approvals = run.client && !run.client->permissions().isEmpty();
    const auto status = thread ? thread->archived ? QString("已归档") :
                        approvals ? QString("待审批") :
                        run.busy() || run.phase == Phase::NeedAuth ? run.status :
                        thread->interrupted ? QString("上轮未确认") :
                        run.phase == Phase::Ready ? run.status : QString("未连接") : QString();
    status_->setText(status.isEmpty() ? QString() : "•  " + status);
    status_->setToolTip(run.status);
    const auto state = approvals ? QString("approval") : run.busy() || run.phase == Phase::Ready ? QString("active") : QString();
    if (status_->property("state").toString() != state) {
        status_->setProperty("state", state);
        status_->style()->unpolish(status_);
        status_->style()->polish(status_);
    }
    title_->setVisible(thread);
    reviewButton_->setVisible(thread);
    newButton_->setVisible(!project_.isEmpty() && (!thread || !thread->messages.isEmpty()));
    moreButton_->setVisible(thread || !project_.isEmpty());
    homeButton_->setChecked(current_.isEmpty() && project_.isEmpty());

    if (!thread) {
        pages_->setCurrentIndex(0);
        home_->rebuild(project_);
        displayReview(false);
        return;
    }
    pages_->setCurrentIndex(1);
    const bool fresh = thread->messages.isEmpty() && thread->remote.isEmpty() &&
                       !thread->archived && run.phase != Phase::Running;
    welcome_->setVisible(fresh);
    welcomeSpace_->setVisible(fresh);
    transcript_->setVisible(!fresh);
    const bool changed = stageId_ != current_;
    stageId_ = current_;
    if (changed)
        transcriptLimit_ = 120;
    if (input_->toPlainText() != thread->draft.text) {
        const QSignalBlocker guard(input_);
        input_->setPlainText(thread->draft.text);
        input_->moveCursor(QTextCursor::End);
    }
    const bool editable = !thread->archived && run.phase != Phase::Preparing;
    composer_->setAcceptDrops(editable);
    input_->setEnabled(editable);
    add_->setEnabled(editable);
    if (displayedFiles_ != thread->draft.files || changed)
        renderFiles(thread->draft);
    if (files_->isVisible())
        files_->setEnabled(editable);
    {
        const QSignalBlocker guard(isolated_);
        if (isolated_->isChecked() != thread->isolated) {
            isolated_->setChecked(thread->isolated);
            setLineIcon(isolated_, thread->isolated ? Icon::Branch : Icon::Folder);
        }
        isolated_->setText(thread->isolated ? "独立工作树" : "项目目录");
        isolated_->setEnabled(!thread->archived && thread->remote.isEmpty() && !run.busy() && !run.client);
    }
    const bool running = run.phase == Phase::Running;
    const bool waiting = run.phase == Phase::Connecting || run.phase == Phase::Preparing;
    connect_->setVisible(!thread->archived && (run.phase == Phase::Offline || run.phase == Phase::NeedAuth));
    connect_->setText(run.phase == Phase::NeedAuth ? "Google 登录" : thread->remote.isEmpty() ? "连接" : "恢复会话");
    stop_->setVisible(!thread->archived && (run.busy() || run.phase == Phase::NeedAuth));
    stop_->setEnabled(!run.cancelPending && run.status != "正在停止…");
    queue_->setVisible(running);
    queue_->setEnabled(!thread->draft.empty() && thread->queued.empty() && !run.queueInFlight);
    send_->setText(running ? "补充" : "");
    send_->setAccessibleName(running ? "补充指令" : "发送消息");
    send_->setIcon(lineIcon(Icon::Send, colors().text.lightness() > 160 ? QColor("#19243b") : QColor("#ffffff")));
    send_->setIconSize({17, 17});
    send_->setEnabled(editable && !waiting && run.phase != Phase::NeedAuth && !thread->draft.empty() &&
                      (!running || (run.client && run.client->canSteer() && thread->draft.files.isEmpty() &&
                       !thread->draft.text.trimmed().isEmpty())));
    const auto options = run.client ? run.client->options() : QList<Config>{};
    QString modelLabel = "模型";
    QString effortLabel;
    bool modelMenu = false;
    for (const auto &option : options) {
        const auto selected = std::find_if(option.choices.cbegin(), option.choices.cend(),
                                          [&](const auto &choice) { return choice.first == option.value; });
        const auto name = selected == option.choices.cend() ? option.value : selected->second;
        if (option.category == "model" && !name.isEmpty())
            modelLabel = name;
        if (option.category == "thought_level")
            effortLabel = name.isEmpty() ? "思考" : name;
        else
            modelMenu = true;
    }
    model_->setText(model_->fontMetrics().elidedText(modelLabel, Qt::ElideRight, 90));
    model_->setToolTip(modelLabel);
    model_->setEnabled(run.client && run.client->ready() && modelMenu);
    effort_->setVisible(!effortLabel.isEmpty());
    effort_->setEnabled(run.phase == Phase::Ready);
    effort_->setText(effort_->fontMetrics().elidedText(effortLabel, Qt::ElideRight, 58));
    effort_->setToolTip("调整思考强度 · " + effortLabel);
    const auto commands = run.client && editable ? run.client->commands() : QList<QPair<QString, QString>>{};
    commands_->setEnabled(!commands.isEmpty());
    static_cast<ComposerEdit *>(input_)->setCommands(commands);
    composer_->setVisible(!thread->archived);
    queuedCard_->setVisible(!thread->archived && !thread->queued.empty());
    const auto queuedLabel = thread->queued.text.simplified().left(55);
    queuedText_->setText(QString("%1 · %2").arg(run.autoRunQueued ? "下一轮" : "已暂停",
                                              queuedLabel.isEmpty() ? "附件消息" : queuedLabel));
    queuedText_->setToolTip(thread->queued.text);
    runQueued_->setEnabled(!run.busy() && run.phase != Phase::NeedAuth);
    restoreQueued_->setEnabled(run.source != SendSource::Queue && !run.queueInFlight);
    paintApprovals(run);
    if (reviewId_ != current_) {
        reviewId_ = current_;
        review_->bind(current_);
    }
    review_->updateActions();
    const bool refresh = !changed && (lastPhase_ == Phase::Running || lastPhase_ == Phase::Connecting) &&
                         (run.phase == Phase::Ready || run.phase == Phase::Offline);
    lastPhase_ = run.phase;
    if (refresh && review_->isVisible())
        review_->refresh();
    if (changed)
        scheduleTranscript();
}

void Window::scheduleTranscript()
{
    if (!transcriptTimer_.isActive())
        transcriptTimer_.start();
}

void Window::renderTranscript()
{
    const auto *thread = controller_.thread(current_);
    const auto range = transcript_->verticalScrollBar();
    const int previousPosition = range->value();
    const bool same = current_ == transcriptId_;
    if (same && transcript_->textCursor().hasSelection()) {
        transcriptDeferred_ = true;
        if (review_->isVisible())
            review_->updateActivity();
        return;
    }
    transcriptDeferred_ = false;
    const bool atEnd = !same || range->maximum() - previousPosition <= 28;
    transcriptId_ = current_;
    if (!thread) {
        transcript_->clear();
        return;
    }

    const auto c = colors();
    auto *document = transcript_->document();
    document->clear();
    QTextCursor cursor(document);
    QTextCharFormat base;
    base.setForeground(c.text);
    base.setFontPointSize(12.4);
    QTextCharFormat labelStyle;
    labelStyle.setForeground(c.accent);
    labelStyle.setFontWeight(QFont::DemiBold);
    labelStyle.setFontPointSize(10.5);
    if (thread->messages.isEmpty()) {
        if (!thread->remote.isEmpty())
            cursor.insertHtml(QString("<span style='color:%1'>恢复会话以加载正文。</span>").arg(c.muted.name()));
    }

    const auto first = qMax<qsizetype>(0, thread->messages.size() - transcriptLimit_);
    if (first > 0)
        cursor.insertHtml(QString("<a href='orbite:older' style='color:%1'>显示更早的消息</a>").arg(c.accent.name()));
    for (auto index = first; index < thread->messages.size(); ++index) {
        const auto &message = thread->messages[index];
        if (message.role == "assistant" && message.text.isEmpty() && message.tools.isEmpty())
            continue;
        if (message.role == "user" || message.role == "steer") {
            QTextFrameFormat bubble;
            bubble.setWidth(QTextLength(QTextLength::PercentageLength, 100));
            bubble.setBackground(c.userMessage);
            bubble.setTopMargin(8);
            bubble.setPadding(13);
            auto *frame = cursor.insertFrame(bubble);
            auto inside = frame->firstCursorPosition();
            inside.insertText(message.role == "steer" ? "你 · 补充" : "你", labelStyle);
            QTextBlockFormat body;
            body.setTopMargin(5);
            inside.insertBlock(body, base);
            inside.insertText(message.text, base);
            if (!message.attachments.isEmpty()) {
                inside.insertBlock(body, base);
                inside.insertHtml(QString("<span style='color:%1; font-size:10pt'>附件  %2</span>")
                                      .arg(c.muted.name(), message.attachments.join(" · ").toHtmlEscaped()));
            }
            cursor.movePosition(QTextCursor::End);
            continue;
        }

        QTextBlockFormat heading;
        heading.setTopMargin(13);
        heading.setBottomMargin(5);
        cursor.insertBlock(heading, labelStyle);
        cursor.insertText(message.role == "assistant" ? "Antigravity" : "会话状态", labelStyle);
        QTextBlockFormat paragraph;
        paragraph.setBottomMargin(5);
        cursor.insertBlock(paragraph, base);
        if (message.role == "assistant") {
            const QTextDocument::MarkdownFeatures features{QTextDocument::MarkdownDialectGitHub, QTextDocument::MarkdownNoHTML};
            cursor.insertFragment(QTextDocumentFragment::fromMarkdown(message.text, features));
        } else {
            cursor.insertText(message.text, base);
        }
        if (!message.tools.isEmpty()) {
            cursor.insertBlock(paragraph, base);
            cursor.insertHtml(QString("<a href='orbite:activity' style='color:%1; text-decoration:none; font-size:10pt'>"
                                      "›  %2 项工具操作</a>").arg(c.muted.name()).arg(message.tools.size()));
        }
    }
    const auto run = controller_.runtime(current_);
    if (thread->interrupted && run.phase != Phase::Running) {
        cursor.insertBlock();
        cursor.insertHtml(QString("<span style='color:%1'>上轮运行中断。请在审阅中核对工作区改动。</span>").arg(c.muted.name()));
    } else if (run.phase == Phase::Running && !thread->messages.isEmpty() &&
               thread->messages.last().role == "assistant" && thread->messages.last().text.isEmpty() &&
               thread->messages.last().tools.isEmpty()) {
        cursor.insertBlock();
        cursor.insertHtml(QString("<span style='color:%1'>Antigravity 正在处理…</span>").arg(c.muted.name()));
    }
    range->setValue(atEnd ? range->maximum() : previousPosition);
    if (review_->isVisible())
        review_->updateActivity();
}

void Window::paintApprovals(const Runtime &run)
{
    const auto current = approvalChoice_->currentData().toString();
    shownPermissions_ = run.client ? run.client->permissions() : QMap<QString, Permission>{};
    const auto keys = shownPermissions_.keys();
    approvalCard_->setVisible(!keys.isEmpty());
    if (permissionKeys_ != keys || (approvalChoice_->count() &&
        approvalChoice_->itemText(0) != shownPermissions_.value(keys.value(0)).title)) {
        const QSignalBlocker guard(approvalChoice_);
        permissionKeys_ = keys;
        approvalChoice_->clear();
        for (const auto &approval : std::as_const(shownPermissions_))
            approvalChoice_->addItem(approval.title, approval.key);
        approvalChoice_->setCurrentIndex(qMax(0, approvalChoice_->findData(current)));
    }
    const auto approval = shownPermissions_.value(approvalChoice_->currentData().toString());
    if (approvalBody_->toPlainText() != approval.detail)
        approvalBody_->setPlainText(approval.detail);
    allow_->setEnabled(!approval.allow.isEmpty());
}

void Window::appendFiles(const QStringList &paths, const QImage &image)
{
    const auto *thread = controller_.thread(current_);
    if (!thread || !input_->isEnabled())
        return;
    Draft draft = thread->draft;
    QStringList rejected;
    for (const auto &path : paths) {
        if (draft.files.size() >= 16) {
            rejected << "每次最多添加 16 个附件。";
            break;
        }
        try {
            const auto file = describeAttachment(path);
            if (!draft.files.contains(file.path))
                draft.files << file.path;
        } catch (const Failure &error) {
            rejected << error.text;
        }
    }
    if (!image.isNull() && draft.files.size() < 16) {
        const auto dir = controller_.profileDirectory() + "/clipboard";
        if (qint64(image.width()) * image.height() > 32 * 1024 * 1024) {
            rejected << "剪贴板截图过大。";
        } else if (!QDir().mkpath(dir) || QFileInfo(dir).isSymLink()) {
            rejected << "无法创建剪贴板截图缓存。";
        } else {
            const auto path = dir + "/截图-" + newId().left(8) + ".png";
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly) ||
                !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
                !image.save(&output, "PNG") || !output.commit()) {
                rejected << "无法保存剪贴板截图。";
            } else {
                draft.files << path;
            }
        }
    }
    controller_.setDraft(current_, draft);
    info(rejected.join("  "));
}

void Window::chooseAttachment(bool folder)
{
    const auto root = controller_.projects().value(project_).root;
    if (folder) {
        const auto path = QFileDialog::getExistingDirectory(this, "添加文件夹", root);
        if (!path.isEmpty())
            appendFiles({path}, {});
    } else {
        const auto paths = QFileDialog::getOpenFileNames(this, "添加文件", root);
        if (!paths.isEmpty())
            appendFiles(paths, {});
    }
}

void Window::renderFiles(const Draft &draft)
{
    displayedFiles_ = draft.files;
    auto *layout = qobject_cast<QGridLayout *>(files_->layout());
    while (auto *item = layout->takeAt(0)) {
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }
    const auto id = current_;
    int index = 0;
    for (const auto &path : draft.files) {
        const auto name = QFileInfo(path).fileName();
        auto *chip = button(name.size() > 18 ? name.left(15) + "…  ×" : name + "  ×", files_);
        chip->setProperty("small", true);
        chip->setToolTip(path + "\n点击移除附件");
        layout->addWidget(chip, index / 4, index % 4);
        ++index;
        connect(chip, &QPushButton::clicked, this, [this, id, path] {
            if (const auto *thread = controller_.thread(id)) {
                auto draft = thread->draft;
                draft.files.removeAll(path);
                controller_.setDraft(id, draft);
            }
        });
    }
    files_->setVisible(!draft.files.isEmpty());
}

void Window::buildModelMenu()
{
    modelMenu_->clear();
    const auto run = controller_.runtime(current_);
    if (!run.client)
        return;
    const auto id = current_;
    for (const auto &option : run.client->options()) {
        if (option.category == "thought_level")
            continue;
        auto *group = modelMenu_->addMenu(option.label);
        group->setEnabled(run.phase == Phase::Ready);
        for (const auto &[value, label] : option.choices) {
            auto *action = group->addAction(label);
            action->setCheckable(true);
            action->setChecked(option.value == value);
            connect(action, &QAction::triggered, this, [this, id, key = option.id, value] {
                controller_.configure(id, key, value);
            });
        }
    }
}

void Window::buildEffortMenu()
{
    effortMenu_->clear();
    const auto id = current_;
    const auto run = controller_.runtime(id);
    if (!run.client || run.phase != Phase::Ready)
        return;
    for (const auto &option : run.client->options()) {
        if (option.category != "thought_level")
            continue;
        auto *pane = new QWidget(effortMenu_);
        pane->setMinimumWidth(214);
        auto *layout = new QVBoxLayout(pane);
        layout->setContentsMargins(10, 9, 10, 9);
        layout->addWidget(label("思考强度", "caption", pane));
        auto *level = label("", "sectionTitle", pane);
        layout->addWidget(level);
        auto *slider = new QSlider(Qt::Horizontal, pane);
        slider->setRange(0, option.choices.size() - 1);
        slider->setTickPosition(QSlider::TicksBelow);
        const auto current = std::find_if(option.choices.cbegin(), option.choices.cend(),
                                         [&](const auto &entry) { return entry.first == option.value; });
        slider->setValue(current == option.choices.cend() ? 0 : std::distance(option.choices.cbegin(), current));
        level->setText(option.choices[slider->value()].second);
        connect(slider, &QSlider::valueChanged, pane, [level, choices = option.choices](int index) {
            level->setText(choices[index].second);
        });
        layout->addWidget(slider);
        auto *apply = button("应用", pane, true);
        layout->addWidget(apply, 0, Qt::AlignRight);
        connect(apply, &QPushButton::clicked, this, [this, id, slider, option] {
            controller_.configure(id, option.id, option.choices[slider->value()].first);
            effortMenu_->close();
        });
        auto *embedded = new QWidgetAction(effortMenu_);
        embedded->setDefaultWidget(pane);
        effortMenu_->addAction(embedded);
        return;
    }
}

void Window::buildCommandMenu()
{
    commandMenu_->clear();
    const auto run = controller_.runtime(current_);
    if (!run.client)
        return;
    const auto id = current_;
    const auto text = input_->toPlainText().trimmed();
    const auto filter = text.startsWith('/') && !text.contains(' ') ? text.mid(1) : QString();
    for (const auto &[name, detail] : run.client->commands()) {
        if (!name.startsWith(filter, Qt::CaseInsensitive))
            continue;
        auto *item = commandMenu_->addAction("/" + name);
        item->setToolTip(detail);
        connect(item, &QAction::triggered, this, [this, id, name] {
            if (id == current_ && input_->isEnabled()) {
                input_->setPlainText("/" + name + " ");
                input_->moveCursor(QTextCursor::End);
                input_->setFocus();
            }
        });
    }
    commandMenu_->setToolTipsVisible(true);
    if (commandMenu_->isEmpty())
        commandMenu_->addAction("没有匹配的命令")->setEnabled(false);
}

void Window::threadMenu(const QPoint &position, const QString &id)
{
    QMenu menu(this);
    const auto *thread = controller_.thread(id);
    if (thread) {
        const auto run = controller_.runtime(id);
        menu.addAction("重命名…", this, [this, id, title = thread->title] {
            bool ok = false;
            const auto name = QInputDialog::getText(this, "重命名会话", "会话名称", QLineEdit::Normal, title, &ok);
            if (ok)
                controller_.rename(id, name);
        });
        menu.addAction(thread->pinned ? "取消置顶" : "置顶", this, [this, id, pinned = thread->pinned] {
            controller_.setPinned(id, !pinned);
        });
        menu.addAction(thread->archived ? "取消归档" : "归档", this, [this, id, archived = thread->archived] {
            controller_.setArchived(id, !archived);
            if (!archived)
                selectThread({});
            else
                archiveFilter_->setChecked(false);
            rebuildNavigation();
        })->setEnabled(!run.busy());
        if (!thread->archived) {
            auto *cua = menu.addAction("为此会话启用 Computer Use");
            cua->setCheckable(true);
            cua->setChecked(thread->useCua);
            cua->setEnabled(!run.busy());
            connect(cua, &QAction::triggered, this, [this, id](bool checked) {
                action([&] { controller_.setCua(id, checked); });
            });
        }
        menu.addSeparator();
    }
    if (!project_.isEmpty()) {
        const auto project = controller_.projects().value(project_);
        menu.addAction("在文件管理器中打开项目", this, [project] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(project.root));
        });
        menu.addAction("导入该项目的 CLI 会话…", this, [this, project] {
            info("正在读取该项目的 Antigravity 会话…");
            controller_.listRemote(project.id);
        });
    }
    menu.addAction("新会话", this, &Window::createSession);
    menu.exec(position);
}

void Window::showSettings()
{
    action([this] {
        QDialog dialog(this);
        dialog.setWindowTitle("Orbite 设置");
        dialog.setMinimumWidth(520);
        auto *layout = new QVBoxLayout(&dialog);
        auto askPath = [&dialog](QLineEdit *input) {
            const auto path = QFileDialog::getOpenFileName(&dialog, "选择可执行文件", input->text());
            if (!path.isEmpty())
                input->setText(path);
        };
        auto *cli = new QLineEdit(controller_.setting("agy"), &dialog);
        auto *driver = new QLineEdit(controller_.setting("driver"), &dialog);
        auto addPath = [&](const QString &title, QLineEdit *input, const QString &detected) {
            layout->addWidget(label(title, "caption", &dialog));
            auto *row = new QHBoxLayout;
            input->setPlaceholderText(detected.isEmpty() ? "自动查找未发现可执行文件" : detected);
            row->addWidget(input, 1);
            auto *choose = button("选择…", &dialog);
            row->addWidget(choose);
            connect(choose, &QPushButton::clicked, &dialog, [=] { askPath(input); });
            layout->addLayout(row);
        };
        addPath("Antigravity CLI", cli, controller_.executable());
        addPath("Cua Driver", driver, controller_.driver());
        layout->addWidget(label("界面外观", "caption", &dialog));
        auto *appearance = new QComboBox(&dialog);
        appearance->addItem("跟随系统", "system");
        appearance->addItem("浅色 · 暖纸", "light");
        appearance->addItem("深色 · 夜蓝", "dark");
        appearance->setCurrentIndex(qMax(0, appearance->findData(controller_.setting("theme"))));
        layout->addWidget(appearance);
        auto *install = button("查看 Antigravity CLI 安装说明", &dialog);
        layout->addWidget(install);
        connect(install, &QPushButton::clicked, &dialog, [] {
            QDesktopServices::openUrl(QUrl("https://antigravity.google/docs/cli-install"));
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const auto cliPath = cli->text().trimmed();
        const auto cuaPath = driver->text().trimmed();
        if ((!cliPath.isEmpty() && findTool("agy", cliPath).isEmpty()) ||
            (!cuaPath.isEmpty() && findTool("cua-driver", cuaPath).isEmpty()))
            throw Failure("设置未保存。请填写有效的可执行文件绝对路径。");
        controller_.setSetting("agy", cliPath);
        controller_.setSetting("driver", cuaPath);
        controller_.setSetting("theme", appearance->currentData().toString());
        applyAppearance(appearance->currentData().toString());
        info("设置已保存，程序路径会在下次连接时使用。");
    });
}

void Window::showExtension()
{
    action([this] {
        auto *dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle("Computer Use");
        dialog->setMinimumWidth(500);
        auto *layout = new QVBoxLayout(dialog);
        auto *text = new QPlainTextEdit(dialog);
        text->setReadOnly(true);
        text->setMinimumHeight(170);
        text->setPlainText("检测本机已安装的驱动、MCP 工具和系统权限。\n驱动仅在会话菜单中单独启用。");
        layout->addWidget(text);
        auto *row = new QHBoxLayout;
        auto *probe = button("检测本机", dialog, true);
        auto *grant = button("打开授权流程", dialog);
        auto *test = button("运行官方自测", dialog);
        auto *docs = button("安装说明", dialog);
        row->addWidget(probe);
        row->addWidget(grant);
        row->addWidget(test);
        row->addWidget(docs);
        layout->addLayout(row);
        auto *readOnly = button("填入只读联调指令", dialog);
        readOnly->setEnabled(false);
        layout->addWidget(readOnly, 0, Qt::AlignLeft);
        connect(readOnly, &QPushButton::clicked, dialog, [this, dialog, text] {
            const auto *thread = controller_.thread(current_);
            if (!thread || !thread->useCua || thread->archived || controller_.runtime(current_).busy()) {
                text->setPlainText("请在一个空闲会话的菜单中启用 Computer Use。");
                return;
            }
            if (!thread->draft.empty()) {
                text->setPlainText("当前会话已有草稿。请先处理草稿，再填入只读检查指令。");
                return;
            }
            controller_.setDraft(current_, {R"(仅调用 Cua Driver 的 check_permissions，参数为 {"prompt": false}。只报告返回的权限状态。不要截屏、执行桌面动作或请求权限。)", {}});
            dialog->close();
            input_->setFocus();
        });
        const auto allow = [this, text]() {
            if (!controller_.driverCheckAllowed()) {
                text->setPlainText("Computer Use 正由其他会话或诊断操作使用。");
                return QString();
            }
            const auto driver = controller_.driver();
            if (driver.isEmpty()) {
                text->setPlainText("找不到 cua-driver。请安装驱动或在设置中指定路径。");
                return QString();
            }
            controller_.setDriverTesting(true);
            text->setPlainText("等待驱动返回结果…");
            return driver;
        };
        connect(probe, &QPushButton::clicked, dialog, [this, text, readOnly, allow] {
            readOnly->setEnabled(false);
            const auto driver = allow();
            if (driver.isEmpty())
                return;
            runJob(this, [driver] { return inspectDriver(driver); },
                   [this, output = QPointer<QPlainTextEdit>(text),
                    compose = QPointer<QPushButton>(readOnly)](const JobResult<DriverReport> &result) {
                controller_.setDriverTesting(false);
                if (!output)
                    return;
                if (!result.value) {
                    output->setPlainText(result.error);
                    return;
                }
                const auto &report = *result.value;
                const auto state = [&](const QString &key) {
                    const auto value = report.permissions.value(key);
                    return !value.isBool() ? QString("未确认") : value.toBool() ? QString("已授权") : QString("未授权");
                };
                QStringList lines{QString("驱动版本  %1").arg(report.version),
                                  QString("MCP  %1 · %2 项工具").arg(report.protocol).arg(report.toolCount),
                                  "辅助功能  " + state("accessibility"), "屏幕录制  " + state("screen_recording")};
                lines << (report.readOnlyPermissionCheck ? "只读联调  可从会话发起，连接效果待验证" : "只读联调  驱动未报告兼容的接口");
                compose->setEnabled(report.readOnlyPermissionCheck);
                if (!report.permissionSource.isEmpty())
                    lines << "权限归属  " + report.permissionSource;
                if (!report.missingTools.isEmpty())
                    lines << "缺少工具  " + report.missingTools.join("、");
                lines += report.issues;
                output->setPlainText(lines.join('\n'));
            });
        });
        const auto invoke = [this, text, allow](bool selfTest) {
            const auto driver = allow();
            if (driver.isEmpty())
                return;
            runJob(this, [driver, selfTest] {
                if (selfTest)
                    driverSelfTest(driver);
                else
                    driverAuthorize(driver);
                return true;
            }, [this, output = QPointer<QPlainTextEdit>(text), selfTest](const JobResult<bool> &result) {
                controller_.setDriverTesting(false);
                if (output)
                    output->setPlainText(result.value ? selfTest ? "驱动官方自测通过。" : "授权流程已完成，请重新检测权限。" : result.error);
            });
        };
        connect(grant, &QPushButton::clicked, dialog, [invoke] { invoke(false); });
        connect(test, &QPushButton::clicked, dialog, [invoke] { invoke(true); });
        connect(docs, &QPushButton::clicked, dialog, [] {
            QDesktopServices::openUrl(QUrl("https://cua.ai/docs/cua-driver/quickstart"));
        });
        dialog->show();
    });
}

void Window::showRemote(const QString &project, const QList<RemoteThread> &rows)
{
    QSet<QString> imported;
    for (const auto &thread : controller_.threads()) {
        if (thread.project == project)
            imported.insert(thread.remote);
    }
    QDialog dialog(this);
    dialog.setWindowTitle("导入 Antigravity 会话");
    dialog.resize(560, 400);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(label("选择要导入的会话。正文将在恢复会话时读取。", "caption", &dialog));
    auto *sessions = new QListWidget(&dialog);
    sessions->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QList<RemoteThread> available;
    for (const auto &session : rows) {
        if (imported.contains(session.id))
            continue;
        imported.insert(session.id);
        const auto title = session.title.isEmpty() ? "未命名会话" : session.title;
        auto *item = new QListWidgetItem(title + "  ·  " + session.updated.toLocalTime().toString("yyyy-MM-dd HH:mm"), sessions);
        item->setData(Qt::UserRole, available.size());
        item->setToolTip(session.id);
        available << session;
    }
    if (available.isEmpty()) {
        info("这个项目没有尚未导入的 Antigravity 会话。");
        return;
    }
    layout->addWidget(sessions, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("导入");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QList<RemoteThread> selected;
    for (const auto *item : sessions->selectedItems())
        selected << available[item->data(Qt::UserRole).toLongLong()];
    action([&] {
        const auto count = controller_.importRemote(project, selected);
        info(QString("已导入 %1 个会话。").arg(count));
    });
}

void Window::importLegacy()
{
    const auto path = QFileDialog::getOpenFileName(this, "导入 Orbite 0.7 的 JSON 资料", {}, "JSON (*.json)");
    if (path.isEmpty())
        return;
    action([&] {
        QFile source(path);
        if (!source.open(QIODevice::ReadOnly) || source.size() > 128 * 1024 * 1024)
            throw Failure("无法读取迁移资料。");
        const auto count = controller_.importLegacy(source.readAll());
        if (project_.isEmpty() && !controller_.projects().isEmpty()) {
            project_ = controller_.projects().firstKey();
            selectThread({});
        }
        rebuildNavigation();
        info(QString("已导入 %1 个会话。首次使用旧项目时，请重新选择项目目录。").arg(count));
    });
}

void Window::applyAppearance(const QString &choice)
{
    applyTheme(choice);
    refreshLineIcons(this);
    syncThread();
    review_->updateTheme();
    rebuildNavigation();
    scheduleTranscript();
}

void Window::displayReview(bool open)
{
    const QSignalBlocker blocked(reviewButton_);
    const bool show = open && !current_.isEmpty();
    reviewButton_->setChecked(show);
    const bool opening = show && review_->isHidden();
    review_->setVisible(show);
    if (opening) {
        auto *split = qobject_cast<QSplitter *>(review_->parentWidget());
        auto sizes = split->sizes();
        sizes[2] = 370;
        sizes[1] = qMax(420, split->width() - sizes[0] - sizes[2]);
        split->setSizes(sizes);
    }
    if (show) {
        if (reviewId_ != current_) {
            reviewId_ = current_;
            review_->bind(current_);
        } else {
            review_->refresh();
        }
    }
}

void Window::closeEvent(QCloseEvent *event)
{
    if (controller_.applying()) {
        info("正在应用补丁，完成后可以关闭窗口。");
        event->ignore();
        return;
    }
    if (controller_.busy() &&
        QMessageBox::question(this, "退出 Orbite", "仍有会话或操作在运行。退出会终止会话，是否继续？",
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    controller_.stopAll();
    event->accept();
}

}
