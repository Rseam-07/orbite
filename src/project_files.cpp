#include "project_files.h"
#include "common.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStringDecoder>
#include <QTreeView>
#include <QVBoxLayout>

namespace orbite {
namespace {

constexpr qsizetype previewLimit = 256 * 1024;

class ProjectFileModel : public QSortFilterProxyModel {
public:
    ProjectFileModel(QFileSystemModel *files, QObject *parent) : QSortFilterProxyModel(parent), files_(files)
    {
        setSourceModel(files);
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const auto index = files_->index(row, 0, parent);
        const auto name = files_->fileName(index);
        return name.compare(".git", Qt::CaseInsensitive) != 0 &&
               name.compare(".orbit-attachments", Qt::CaseInsensitive) != 0 &&
               !files_->fileInfo(index).isJunction();
    }

private:
    QFileSystemModel *files_;
};

QString previewFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw Failure("无法读取此文件。");
    if (file.size() > previewLimit)
        throw Failure("文件超过 256 KiB。仍可引用其路径。");
    const auto bytes = file.read(previewLimit + 1);
    if (file.error() != QFileDevice::NoError)
        throw Failure("无法读取此文件。");
    if (bytes.size() > previewLimit)
        throw Failure("文件超过 256 KiB。仍可引用其路径。");
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString content = decoder(bytes);
    if (bytes.contains('\0') || decoder.hasError())
        throw Failure("此文件无法作为 UTF-8 文本预览。仍可引用其路径。");
    return content;
}

}

void browseProjectFiles(QWidget *parent, const QString &root,
                        const std::function<void(const QString &, int)> &reference)
{
    auto *dialog = new QDialog(parent);
    dialog->setObjectName("projectFiles");
    dialog->setWindowTitle("项目文件");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(850, 580);
    auto *layout = new QVBoxLayout(dialog);
    auto *location = new QLabel(QDir::toNativeSeparators(root), dialog);
    location->setTextFormat(Qt::PlainText);
    location->setTextInteractionFlags(Qt::TextSelectableByMouse);
    location->setWordWrap(true);
    layout->addWidget(location);

    auto *splitter = new QSplitter(dialog);
    auto *tree = new QTreeView(splitter);
    tree->setObjectName("projectFileTree");
    tree->setAccessibleName("项目文件树");
    auto *files = new QFileSystemModel(dialog);
    files->setResolveSymlinks(false);
    files->setFilter(QDir::AllDirs | QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    auto *model = new ProjectFileModel(files, dialog);
    tree->setModel(model);
    const auto rootIndex = model->mapFromSource(files->setRootPath(root));
    if (!rootIndex.isValid()) {
        dialog->deleteLater();
        throw Failure("无法读取此项目目录。");
    }
    tree->setRootIndex(rootIndex);
    tree->setHeaderHidden(true);
    tree->setUniformRowHeights(true);
    for (int column = 1; column < model->columnCount(); ++column)
        tree->hideColumn(column);

    auto *preview = new QPlainTextEdit(splitter);
    preview->setObjectName("projectFilePreview");
    preview->setAccessibleName("文件内容");
    preview->setReadOnly(true);
    preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    preview->setPlaceholderText("选择文件以预览。");
    splitter->setSizes({260, 560});
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto *insert = buttons->addButton("引用文件", QDialogButtonBox::ActionRole);
    insert->setObjectName("referenceProjectFile");
    insert->setEnabled(false);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    const auto updateReference = [preview, insert] {
        insert->setText(preview->document()->isEmpty() ? QString("引用文件") :
                        QString("引用第 %1 行").arg(preview->textCursor().blockNumber() + 1));
    };
    QObject::connect(preview, &QPlainTextEdit::cursorPositionChanged, dialog, updateReference);

    auto selectedFile = [model, files, tree, root] {
        const auto file = files->fileInfo(model->mapToSource(tree->currentIndex()));
        const auto canonical = file.canonicalFilePath();
        if (!file.isFile() || file.isSymLink() || !withinDirectory(root, canonical))
            return QString();
        return canonical;
    };
    QObject::connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, dialog,
                     [preview, insert, selectedFile, reference, updateReference] {
        preview->clear();
        const auto path = selectedFile();
        insert->setEnabled(reference && !path.isEmpty());
        if (path.isEmpty()) {
            preview->setPlaceholderText("选择文件以预览。");
            updateReference();
            return;
        }
        try {
            preview->setPlaceholderText({});
            preview->setPlainText(previewFile(path));
        } catch (const Failure &error) {
            preview->setPlaceholderText(error.text);
        }
        updateReference();
    });
    QObject::connect(insert, &QPushButton::clicked, dialog, [dialog, root, preview, selectedFile, reference] {
        const auto path = selectedFile();
        if (path.isEmpty() || !reference)
            return;
        const auto relative = QDir(root).relativeFilePath(path);
        const auto line = preview->document()->isEmpty() ? 0 : preview->textCursor().blockNumber() + 1;
        dialog->accept();
        reference(relative, line);
    });
    dialog->open();
}

}
