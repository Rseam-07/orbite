#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <iostream>

namespace {

void write(const QJsonObject &object, bool split = false)
{
    auto message = object;
    message.insert("jsonrpc", "2.0");
    auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    bytes += "\r\n";
    if (split) {
        const auto position = bytes.indexOf(QString("完").toUtf8());
        if (position >= 0) {
            std::cout.write(bytes.constData(), position + 1).flush();
            QThread::msleep(15);
            bytes.remove(0, position + 1);
        }
    }
    std::cout.write(bytes.constData(), bytes.size()).flush();
}

void result(const QJsonValue &id, const QJsonObject &value = {})
{
    write({{"id", id}, {"result", value}});
}

void update(const QString &session, QJsonObject message)
{
    write({{"method", "session/update"}, {"params", QJsonObject{{"sessionId", session}, {"update", message}}}},
          message.value("sessionUpdate") == "agent_message_chunk");
}

void say(const QString &session, const QString &text, const QString &role = "agent")
{
    update(session, {{"sessionUpdate", role + "_message_chunk"}, {"content", QJsonObject{{"type", "text"}, {"text", text}}}});
}

QJsonObject options(const QString &selected = "test", const QString &effort = "medium")
{
    const QJsonArray choices{QJsonObject{{"value", "test"}, {"name", "Fixture"}},
                             QJsonObject{{"value", "reasoning"}, {"name", "Reasoning"}},
                             QJsonObject{{"value", "danger"}, {"name", "Danger"}}};
    const QJsonArray levels{QJsonObject{{"value", "low"}, {"name", "低"}},
                            QJsonObject{{"value", "medium"}, {"name", "中等"}},
                            QJsonObject{{"value", "high"}, {"name", "高"}}};
    const QJsonArray config{
        QJsonObject{{"type", "select"}, {"id", "model"}, {"name", "模型"},
                    {"category", "model"}, {"currentValue", selected}, {"options", choices}},
        QJsonObject{{"type", "select"}, {"id", "effort"}, {"name", "思考强度"},
                    {"category", "thought_level"}, {"currentValue", effort}, {"options", levels}}};
    return {{"configOptions", config}};
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    const auto cwd = QDir::currentPath();
    if (arguments.contains("--version")) {
        std::cout << "cua-driver 0.62.1\n";
        return 0;
    }
    if (arguments.contains("permissions")) {
        std::cout << R"({"permissions":{"accessibility":true,"screen_recording":false,"source":{"attribution":"fixture"}}})" << '\n';
        return 0;
    }

    const auto mcp = arguments.contains("mcp");
    bool authenticated = QFileInfo(cwd).fileName() != "auth";
    QString session = "fixture-session";
    QString model = "test";
    QString effort = "medium";
    QJsonValue running;
    bool wait = false;
    for (std::string line; std::getline(std::cin, line);) {
        const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const auto method = request.value("method").toString();
        const auto id = request.value("id");
        const auto params = request.value("params").toObject();
        if (method == "initialize") {
            if (mcp) {
                result(id, {{"protocolVersion", "2025-11-25"}, {"capabilities", QJsonObject{{"tools", QJsonObject{}}}}});
            } else {
                result(id, {{"protocolVersion", 1},
                            {"authMethods", QJsonArray{QJsonObject{{"id", "oauth-personal"}}}},
                            {"agentCapabilities", QJsonObject{{"loadSession", true},
                                {"promptCapabilities", QJsonObject{{"image", true}}},
                                {"sessionCapabilities", QJsonObject{{"list", QJsonObject{}}, {"resume", QJsonObject{}}}}}}});
            }
        } else if (method == "tools/list") {
            if (!params.contains("cursor")) {
                result(id, {{"tools", QJsonArray{QJsonObject{{"name", "list_windows"}}, QJsonObject{{"name", "get_window_state"}}}},
                            {"nextCursor", "tool-page-2"}});
            } else {
                const QJsonObject check{
                    {"name", "check_permissions"},
                    {"inputSchema", QJsonObject{{"properties", QJsonObject{{"prompt", QJsonObject{{"type", "boolean"}}}}}}}};
                result(id, {{"tools", QJsonArray{QJsonObject{{"name", "click"}}, QJsonObject{{"name", "type_text"}}, check}}});
            }
        } else if (method == "malformed") {
            std::cout << "{bad\n" << std::flush;
        } else if (method == "overflow") {
            std::cout << std::string(4096, 'X') << "\n" << std::flush;
        } else if (method == "hang") {
            continue;
        } else if (method == "authenticate") {
            authenticated = true;
            result(id);
        } else if ((method == "session/new" || method == "session/load" || method == "session/list" ||
                    method == "session/resume") && !authenticated) {
            write({{"id", id}, {"error", QJsonObject{{"code", -32000}, {"message", "Authentication required"}}}});
        } else if (method == "session/list") {
            const auto page = params.contains("cursor") ? QString("two") : QString("one");
            QJsonObject value{{"sessions", QJsonArray{QJsonObject{{"sessionId", page}, {"title", "测试会话 " + page}, {"cwd", cwd},
                                                                  {"updatedAt", "2026-11-26T00:00:00Z"}}}}};
            if (page == "one")
                value.insert("nextCursor", "page-2");
            result(id, value);
        } else if (method == "session/resume") {
            session = params.value("sessionId").toString();
            if (session == "load-only") {
                write({{"id", id}, {"error", QJsonObject{{"code", -32601}, {"message", "Resume unavailable"}}}});
            } else {
                result(id, options("reasoning", effort));
            }
        } else if (method == "session/new" || method == "session/load") {
            if (method == "session/load") {
                session = params.value("sessionId").toString();
                say(session, "旧问题", "user");
                say(session, "旧回复");
            }
            update(session, {{"sessionUpdate", "available_commands_update"},
                             {"availableCommands", QJsonArray{QJsonObject{{"name", "plan"}, {"description", "Make a plan"}}}}});
            auto value = options(model, effort);
            value.insert("sessionId", session);
            result(id, value);
        } else if (method == "session/set_config_option") {
            if (params.value("configId") == "effort")
                effort = params.value("value").toString();
            else
                model = params.value("value").toString();
            result(id, options(model, effort));
        } else if (method == "session/prompt") {
            const auto blocks = params.value("prompt").toArray();
            const auto text = blocks.first().toObject().value("text").toString();
            if (wait) {
                say(session, "收到补充");
                result(id, {{"stopReason", "end_turn"}});
            } else if (text.startsWith("wait")) {
                running = id;
                wait = true;
                say(session, "等待中");
            } else if (text.startsWith("disconnect")) {
                say(session, "部分结果");
                return 17;
            } else if (text.startsWith("为 Atlas")) {
                running = id;
                update(session, {{"sessionUpdate", "plan"},
                                 {"entries", QJsonArray{QJsonObject{{"content", "添加测试文件"}, {"status", "in_progress"}}}}});
                write({{"id", "approve"}, {"method", "session/request_permission"},
                       {"params", QJsonObject{{"sessionId", session},
                         {"toolCall", QJsonObject{{"title", "写入 review-notes.txt"},
                             {"rawInput", QJsonObject{{"path", "review-notes.txt"}}}}},
                         {"options", QJsonArray{QJsonObject{{"kind", "allow_once"}, {"optionId", "yes"}},
                                                QJsonObject{{"kind", "allow_always"}, {"optionId", "always"}},
                                                QJsonObject{{"kind", "reject_once"}, {"optionId", "no"}}}}}}});
            } else {
                say(session, "完成。");
                result(id, {{"stopReason", "end_turn"}});
            }
        } else if (method == "session/cancel") {
            wait = false;
            if (!running.isNull() && !running.isUndefined())
                result(running, {{"stopReason", "cancelled"}});
            running = {};
        } else if (id.toString() == "approve") {
            const auto selected = request.value("result").toObject().value("outcome").toObject().value("optionId").toString();
            if (selected == "yes") {
                QFile out(cwd + "/review-notes.txt");
                if (!out.open(QIODevice::WriteOnly))
                    return 21;
                out.write("Qt 工作树验收\n");
                out.close();
                update(session, {{"sessionUpdate", "tool_call_update"}, {"toolCallId", "write-1"},
                                 {"title", "写入 review-notes.txt"}, {"status", "completed"},
                                 {"content", QJsonArray{QJsonObject{{"type", "diff"}, {"path", out.fileName()},
                                                                     {"oldText", ""}, {"newText", "Qt 工作树验收\n"}}}}});
                update(session, {{"sessionUpdate", "plan"},
                                 {"entries", QJsonArray{QJsonObject{{"content", "添加测试文件"}, {"status", "completed"}}}}});
                say(session, "完成。已写入 `review-notes.txt`。\n\n- 笔记保存在当前独立工作树。\n- 在右侧审阅新增文件，可导出补丁或应用到项目。");
                result(running, {{"stopReason", "end_turn"}});
            } else {
                result(running, {{"stopReason", "refusal"}});
            }
            running = {};
        }
    }
    return 0;
}
