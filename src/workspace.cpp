#include "workspace.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>

namespace orbite {
namespace {

constexpr qsizetype maxPatch = 16 * 1024 * 1024;
const QString excludeAttachments = ":(top,exclude).orbit-attachments";

CommandResult git(const QString &cwd, const QStringList &args, bool acceptDiff = false,
                  const QByteArray &input = {}, const QString &index = {}, int timeout = 10000)
{
    const auto program = findTool("git");
    if (program.isEmpty())
        throw Failure("找不到 Git。");
    QStringList flags{"--no-pager", "-c", "core.fsmonitor=false", "-c", "core.hooksPath=/dev/null",
                      "-c", "core.quotePath=false"};
    auto result = runCommand(program, flags + args, cwd, timeout, maxPatch, input, true, index);
    if (result.code && !(acceptDiff && result.code == 1)) {
        const QString detail = QString::fromUtf8(result.error).trimmed().left(4000);
        throw Failure(detail.isEmpty() ? "Git 操作失败。" : detail);
    }
    return result;
}

const QStringList diffArgs{"diff", "--no-ext-diff", "--no-textconv", "--no-color", "--no-renames",
                          "--ignore-submodules=all", "--src-prefix=a/", "--dst-prefix=b/"};

QString gitString(const QString &cwd, const QStringList &args)
{
    auto result = git(cwd, args).output;
    if (result.endsWith('\n'))
        result.chop(1);
    if (result.endsWith('\r'))
        result.chop(1);
    return QString::fromUtf8(result);
}

QString decodePath(const QByteArray &raw)
{
    const QString path = QString::fromUtf8(raw);
    if (path.toUtf8() != raw)
        throw Failure("Git 返回了无法用 UTF-8 表示的路径。");
    return path;
}

bool isAttachment(const QString &path)
{
    return path == ".orbit-attachments" || path.startsWith(".orbit-attachments/");
}

bool validCommit(const QString &hash)
{
    static const QRegularExpression format("^(?:[0-9a-f]{40}|[0-9a-f]{64})$");
    return format.match(hash).hasMatch();
}

QString repositoryRoot(const QString &cwd)
{
    const auto root = canonicalDirectory(gitString(cwd, {"rev-parse", "--show-toplevel"}));
    if (root.isEmpty())
        throw Failure("无法定位 Git 仓库根目录。");
    return root;
}

QString gitRoot(const QString &cwd)
{
    const auto root = repositoryRoot(cwd);
    if (!samePath(root, cwd))
        throw Failure("请选择 Git 仓库根目录。");
    return root;
}

QString commonDirectory(const QString &cwd)
{
    const auto path = canonicalDirectory(gitString(cwd, {"rev-parse", "--path-format=absolute", "--git-common-dir"}));
    if (path.isEmpty())
        throw Failure("无法确认 Git 仓库位置。");
    return path;
}

QString baseFor(const Scope &scope)
{
    if (!scope.base.isEmpty())
        return scope.base;
    try {
        return gitString(scope.cwd, {"rev-parse", "--verify", "HEAD"});
    } catch (const Failure &) {
        // Git recognizes its empty tree by hash, including in SHA-256 repositories.
        return gitString(scope.cwd, {"hash-object", "-t", "tree", "--stdin"});
    }
}

Scope readWorktree(const QString &root, const QString &space, const QString &common)
{
    if (!samePath(canonicalDirectory(space), space))
        throw Failure("工作树资料目录无效。");
    QFile manifest(space + "/orbit.json");
    if (QFileInfo(manifest).isSymLink() || !manifest.open(QIODevice::ReadOnly) || manifest.size() > 32768)
        throw Failure("找不到有效的工作树记录。");
    const auto data = QJsonDocument::fromJson(manifest.readAll()).object();
    const auto base = data.value("base").toString();
    const auto cwd = space + "/checkout";
    if (!validCommit(base) || !samePath(data.value("common").toString(), common))
        throw Failure("工作树记录与项目不匹配。");
    if (!samePath(canonicalDirectory(cwd), cwd) || !samePath(gitRoot(cwd), cwd) ||
        !samePath(commonDirectory(cwd), common))
        throw Failure("工作树目录与项目不匹配。");
    return {root, cwd, base, gitString(cwd, {"rev-parse", "--abbrev-ref", "HEAD"})};
}

QByteArray status(const QString &cwd, const QStringList &paths = {})
{
    QStringList args{"--literal-pathspecs", "status", "--porcelain=v1", "-z", "--untracked-files=all", "--no-renames",
                     "--ignore-submodules=all", "--"};
    return git(cwd, args + paths).output;
}

QStringList patchPaths(const QString &cwd, const QByteArray &patch)
{
    QStringList paths;
    if (patch.isEmpty())
        return paths;
    for (const auto &entry : git(cwd, {"apply", "--numstat", "-z", "-"}, false, patch).output.split('\0')) {
        if (entry.isEmpty())
            continue;
        const auto first = entry.indexOf('\t');
        const auto second = first < 0 ? -1 : entry.indexOf('\t', first + 1);
        if (second < 0)
            throw Failure("无法读取补丁的文件清单。");
        const auto path = decodePath(entry.mid(second + 1));
        if (!workspaceRelativePath(path) || isAttachment(path))
            throw Failure("补丁包含无法交付的路径。");
        paths << path;
    }
    return paths;
}

}

bool workspaceRelativePath(const QString &path)
{
    const auto parts = path.split('/');
    return !path.isEmpty() && !QDir::isAbsolutePath(path) &&
           !parts.contains("..") && !parts.contains(".") && !parts.contains("");
}

bool pathsOverlap(const QString &a, const QString &b)
{
    return withinDirectory(a, b) || withinDirectory(b, a);
}

Scope openScope(const QString &root, const QString &profile, const QString &id,
                bool isolated, bool create)
{
    if (root.isEmpty() || !samePath(canonicalDirectory(root), root) || QDir(root).isRoot() ||
        samePath(root, canonicalDirectory(QDir::homePath())))
        throw Failure("项目目录不可用，请重新选择。");
    if (!isolated)
        return {root, root, {}, {}};
    if (!validId(id))
        throw Failure("工作树会话 ID 无效。");

    gitRoot(root);
    const auto common = commonDirectory(root);
    const auto storage = canonicalDirectory(profile);
    if (storage.isEmpty())
        throw Failure("找不到本地资料目录。");
    const auto worktrees = storage + "/worktrees";
    const auto space = worktrees + "/" + id;
    if (QFileInfo::exists(space + "/checkout"))
        return readWorktree(root, space, common);
#ifdef Q_OS_MACOS
    const auto legacy = QDir::homePath() + "/Library/Application Support/orbit-antigravity/worktrees/" + id;
    if (QFileInfo::exists(legacy + "/checkout"))
        return readWorktree(root, legacy, common);
#endif
    if (!create)
        throw Failure("工作树会在连接此会话时创建。");
    if (!QDir().mkpath(space) || !samePath(canonicalDirectory(space), space))
        throw Failure("无法准备工作树目录。");

    const auto manifestPath = space + "/orbit.json";
    QString base;
    if (QFileInfo::exists(manifestPath)) {
        QFile manifest(manifestPath);
        if (QFileInfo(manifest).isSymLink() || !manifest.open(QIODevice::ReadOnly) || manifest.size() > 32768)
            throw Failure("无法读取未完成的工作树记录。");
        const auto recorded = QJsonDocument::fromJson(manifest.readAll()).object();
        base = recorded.value("base").toString();
        if (!samePath(recorded.value("common").toString(), common) || !validCommit(base))
            throw Failure("工作树记录与项目不匹配。");
    } else {
        try {
            base = gitString(root, {"rev-parse", "--verify", "HEAD"});
        } catch (const Failure &) {
            throw Failure("项目至少需要一个 Git 提交才能创建独立工作树。");
        }
        if (!validCommit(base))
            throw Failure("Git 工作树基线无效。");
        QSaveFile manifest(manifestPath);
        const auto bytes = QJsonDocument(QJsonObject{{"common", common}, {"base", base}}).toJson();
        if (!manifest.open(QIODevice::WriteOnly) ||
            !manifest.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
            manifest.write(bytes) != bytes.size() || !manifest.commit())
            throw Failure("无法保存工作树记录。");
    }

    const QString branch = "orbite/" + id;
    git(root, {"worktree", "add", "-b", branch, space + "/checkout", base}, false, {}, {}, 90000);
    return readWorktree(root, space, common);
}

Review readReview(const Scope &scope)
{
    const auto root = repositoryRoot(scope.cwd);
    const auto prefix = samePath(root, scope.cwd) ? QString() : QDir(root).relativeFilePath(scope.cwd);
    Review review;
    try {
        review.branch = gitString(scope.cwd, {"rev-parse", "--abbrev-ref", "HEAD"});
    } catch (const Failure &) {
        review.branch = gitString(scope.cwd, {"symbolic-ref", "--short", "HEAD"});
    }
    QMap<QString, Change> files;
    const auto paths = prefix.isEmpty() ? QStringList{} : QStringList{prefix};
    for (const auto &entry : status(root, paths).split('\0')) {
        if (entry.size() < 4)
            continue;
        auto path = decodePath(entry.mid(3));
        if (!prefix.isEmpty()) {
            if (!path.startsWith(prefix + "/"))
                continue;
            path = path.mid(prefix.size() + 1);
        }
        if (!isAttachment(path))
            files.insert(path, {path, QString::fromLatin1(entry.left(2)).trimmed()});
    }
    const auto named = git(scope.cwd, diffArgs + QStringList{"--relative", "--name-status", "-z", baseFor(scope),
                                                            "--", ".", excludeAttachments}).output.split('\0');
    for (qsizetype i = 0; i + 1 < named.size() && !named[i].isEmpty(); i += 2) {
        const auto path = decodePath(named[i + 1]);
        if (!files.contains(path) && !isAttachment(path))
            files.insert(path, {path, QString::fromLatin1(named[i])});
    }
    review.files = files.values();
    return review;
}

QString readPatch(const Scope &scope, const Change &file)
{
    if (!workspaceRelativePath(file.path) || isAttachment(file.path))
        throw Failure("无法审阅此路径。");
    const auto base = baseFor(scope);
    auto tracked = QStringList{"--literal-pathspecs"} + diffArgs + QStringList{"--relative", base, "--", file.path};
    QByteArray patch = git(scope.cwd, tracked).output;
    if (file.status == "??" && patch.isEmpty()) {
        const QStringList args = diffArgs + QStringList{"--no-index", "--", "/dev/null", file.path};
        patch = git(scope.cwd, args, true).output;
    }
    return QString::fromUtf8(patch);
}

Patch buildPatch(const Scope &scope)
{
    if (!validCommit(scope.base) || samePath(scope.root, scope.cwd))
        throw Failure("请在独立工作树中交付改动。");
    gitRoot(scope.cwd);
    QTemporaryDir temporary;
    if (!temporary.isValid())
        throw Failure("无法创建补丁的临时 Git 索引。");
    const QString index = temporary.filePath("index");
    git(scope.cwd, {"read-tree", scope.base}, false, {}, index);
    QByteArray added = git(scope.cwd, diffArgs + QStringList{"--name-only", "-z", "--diff-filter=A",
                                                            scope.base, "--", ".", excludeAttachments}).output;
    added += git(scope.cwd, {"ls-files", "--others", "--exclude-standard", "-z", "--", ".", excludeAttachments}).output;
    if (!added.isEmpty()) {
        git(scope.cwd, {"--literal-pathspecs", "add", "--force", "--intent-to-add",
                        "--pathspec-from-file=-", "--pathspec-file-nul"}, false, added, index, 30000);
    }
    const auto data = git(scope.cwd, diffArgs + QStringList{"--binary", "--full-index", scope.base, "--",
                                                           ".", excludeAttachments}, false, {}, index, 30000).output;
    return {data, patchPaths(scope.cwd, data)};
}

void checkApply(const QString &target, const Patch &patch)
{
    gitRoot(target);
    if (patch.data.isEmpty())
        throw Failure("工作树没有可应用的变更。");
    if (patchPaths(target, patch.data) != patch.paths)
        throw Failure("补丁文件清单发生变化。");
    // Check only files touched by this patch. Other local edits can stay in place.
    for (qsizetype offset = 0; offset < patch.paths.size(); offset += 64) {
        if (!status(target, patch.paths.mid(offset, 64)).isEmpty())
            throw Failure("目标项目中有同名文件的未提交改动。请先处理这些文件。");
    }
    git(target, {"apply", "--check", "--binary", "-"}, false, patch.data);
}

void applyPatch(const QString &target, const Patch &patch)
{
    checkApply(target, patch);
    git(target, {"apply", "--binary", "-"}, false, patch.data);
}

}
