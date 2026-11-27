#include "profile.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace orbite {
namespace {

QJsonObject toJson(const Draft &draft)
{
    return {{"text", draft.text}, {"files", QJsonArray::fromStringList(draft.files)}};
}

QStringList stringList(const QJsonValue &value)
{
    QStringList list;
    for (const auto &item : value.toArray()) {
        if (item.isString())
            list << item.toString();
    }
    return list;
}

Draft readDraft(const QJsonValue &value)
{
    const auto obj = value.toObject();
    return {obj.value("text").toString(), stringList(obj.value("files"))};
}

QJsonObject serialize(const Thread &thread)
{
    QJsonArray messages;
    for (const auto &message : thread.messages) {
        messages.append(QJsonObject{{"role", message.role}, {"text", message.text},
                                    {"attachments", QJsonArray::fromStringList(message.attachments)},
                                    {"tools", message.tools}});
    }
    return {{"title", thread.title}, {"remote", thread.remote}, {"isolated", thread.isolated},
            {"pinned", thread.pinned}, {"archived", thread.archived}, {"useCua", thread.useCua},
            {"interrupted", thread.interrupted}, {"updated", thread.updated.toString(Qt::ISODateWithMs)},
            {"draft", toJson(thread.draft)}, {"queued", toJson(thread.queued)}, {"messages", messages},
            {"plan", thread.plan}, {"diffs", thread.diffs}};
}

Thread deserialize(const QString &id, const QString &project, const QJsonObject &data)
{
    Thread thread;
    thread.id = id;
    thread.project = project;
    thread.title = data.value("title").toString("新会话");
    thread.remote = data.value("remote").toString();
    thread.isolated = data.value("isolated").toBool();
    thread.pinned = data.value("pinned").toBool();
    thread.archived = data.value("archived").toBool();
    thread.useCua = data.value("useCua").toBool();
    thread.interrupted = data.value("interrupted").toBool();
    const auto updated = QDateTime::fromString(data.value("updated").toString(), Qt::ISODate);
    if (updated.isValid())
        thread.updated = updated;
    thread.draft = readDraft(data.value("draft"));
    thread.queued = readDraft(data.value("queued"));
    thread.plan = data.value("plan").toArray();
    thread.diffs = data.value("diffs").toObject();
    for (const auto &value : data.value("messages").toArray()) {
        const auto row = value.toObject();
        thread.messages.append({row.value("role").toString(), row.value("text").toString(),
                                stringList(row.value("attachments")), row.value("tools").toArray()});
    }
    return thread;
}

void execute(QSqlQuery &query)
{
    if (!query.exec())
        throw Failure("无法读写本地会话资料：" + query.lastError().text());
}

void execute(QSqlDatabase database, const QString &statement)
{
    QSqlQuery query(database);
    if (!query.exec(statement))
        throw Failure("无法初始化本地会话资料：" + query.lastError().text());
}

}

Profile::Profile(const QString &directory)
    : directory_(directory), databaseName_(newId())
{
    if (!QDir().mkpath(directory_))
        throw Failure("无法创建 Orbite 的本地资料目录。");
    const QString path = QDir(directory_).filePath("orbite.sqlite3");
    if (!QFile::exists(path)) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
            !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
            throw Failure("无法创建本地会话资料库。");
    }
    try {
        db_ = QSqlDatabase::addDatabase("QSQLITE", databaseName_);
        db_.setDatabaseName(path);
        db_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=2500");
        if (!db_.open())
            throw Failure("无法打开本地会话资料：" + db_.lastError().text());
        execute(db_, "PRAGMA foreign_keys = ON");
        int version;
        {
            QSqlQuery query(db_);
            if (!query.exec("PRAGMA user_version") || !query.next())
                throw Failure("无法读取本地会话资料版本。");
            version = query.value(0).toInt();
        }
        if (version > 1)
            throw Failure("本地资料由较新的 Orbite 写入，请用新版打开。");
        if (!version) {
            if (!db_.transaction())
                throw Failure("无法初始化本地会话资料。");
            execute(db_, "CREATE TABLE IF NOT EXISTS projects (id TEXT PRIMARY KEY, root TEXT UNIQUE NOT NULL)");
            execute(db_, "CREATE TABLE IF NOT EXISTS threads (id TEXT PRIMARY KEY, "
                         "project TEXT NOT NULL REFERENCES projects(id), data BLOB NOT NULL)");
            execute(db_, "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
            execute(db_, "PRAGMA user_version = 1");
            if (!db_.commit())
                throw Failure("无法提交本地资料的初始化。");
        }
    } catch (...) {
        db_.close();
        db_ = {};
        QSqlDatabase::removeDatabase(databaseName_);
        throw;
    }
}

Profile::~Profile()
{
    db_.close();
    db_ = {};
    QSqlDatabase::removeDatabase(databaseName_);
}

QMap<QString, Project> Profile::projects()
{
    QSqlQuery query(db_);
    query.prepare("SELECT id, root FROM projects");
    execute(query);
    QMap<QString, Project> result;
    while (query.next()) {
        const QString id = query.value(0).toString();
        result.insert(id, {id, query.value(1).toString()});
    }
    return result;
}

QMap<QString, Thread> Profile::threads()
{
    QSqlQuery query(db_);
    query.prepare("SELECT id, project, data FROM threads");
    execute(query);
    QMap<QString, Thread> result;
    while (query.next()) {
        const auto id = query.value(0).toString();
        const auto doc = QJsonDocument::fromJson(query.value(2).toByteArray());
        if (!doc.isObject())
            throw Failure("无法读取会话 " + id + " 的本地资料。");
        result.insert(id, deserialize(id, query.value(1).toString(), doc.object()));
    }
    return result;
}

void Profile::save(const Project &project)
{
    QSqlQuery query(db_);
    query.prepare("INSERT INTO projects(id, root) VALUES (?, ?) ON CONFLICT(id) DO UPDATE SET root=excluded.root");
    query.addBindValue(project.id);
    query.addBindValue(project.root);
    execute(query);
}

void Profile::save(const Thread &thread)
{
    QSqlQuery query(db_);
    query.prepare("INSERT INTO threads(id, project, data) VALUES (?, ?, ?) "
                  "ON CONFLICT(id) DO UPDATE SET project=excluded.project, data=excluded.data");
    query.addBindValue(thread.id);
    query.addBindValue(thread.project);
    query.addBindValue(QJsonDocument(serialize(thread)).toJson(QJsonDocument::Compact));
    execute(query);
}

QString Profile::setting(const QString &key)
{
    QSqlQuery query(db_);
    query.prepare("SELECT value FROM settings WHERE key=?");
    query.addBindValue(key);
    execute(query);
    return query.next() ? query.value(0).toString() : QString();
}

void Profile::setSetting(const QString &key, const QString &value)
{
    QSqlQuery query(db_);
    query.prepare("INSERT INTO settings(key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    query.addBindValue(key);
    query.addBindValue(value.isNull() ? QString("") : value);
    execute(query);
}

int Profile::importOrbit07(const QByteArray &source)
{
    const auto data = QJsonDocument::fromJson(source).object();
    if (!data.value("projects").isArray() || !data.value("threads").isArray())
        throw Failure("请选择 Orbite 0.7 导出的 JSON 资料。");

    QMap<QString, QString> roots;
    QMap<QString, QString> known;
    for (const auto &project : projects())
        known.insert(project.root, project.id);
    const auto existing = threads();
    if (!db_.transaction())
        throw Failure("无法启动资料导入。");

    int count = 0;
    try {
        for (const auto &entry : data.value("projects").toArray()) {
            const auto inputPath = QDir::cleanPath(entry.toObject().value("path").toString());
            if (!QDir::isAbsolutePath(inputPath))
                continue;
            const auto canonical = canonicalDirectory(inputPath);
            const auto root = canonical.isEmpty() ? inputPath : canonical;
            if (!known.contains(root)) {
                const auto id = newId();
                save(Project{id, root});
                known.insert(root, id);
                setSetting("unconfirmedProject/" + id, "yes");
            }
            roots.insert(inputPath, known.value(root));
        }
        for (const auto &entry : data.value("threads").toArray()) {
            const auto obj = entry.toObject();
            const auto id = obj.value("id").toString();
            const auto project = roots.value(QDir::cleanPath(obj.value("folder").toString()));
            if (!validId(id) || project.isEmpty() || existing.contains(id))
                continue;
            auto thread = deserialize(id, project, obj);
            const auto entries = obj.value("messages").toArray();
            for (qsizetype i = 0; i < entries.size(); ++i) {
                for (const auto &file : entries.at(i).toObject().value("files").toArray()) {
                    const auto name = file.toObject().value("name").toString();
                    if (!name.isEmpty())
                        thread.messages[i].attachments << name;
                }
            }
            thread.useCua = false;
            thread.draft = {};
            thread.queued = {};
            save(thread);
            ++count;
        }
        if (!db_.commit())
            throw Failure("无法提交导入的会话。");
    } catch (...) {
        db_.rollback();
        throw;
    }
    return count;
}

}
