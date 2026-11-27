#include "computer_use.h"
#include "rpc.h"

#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

namespace orbite {
namespace {

void listTools(const QString &driver, DriverReport &report)
{
    RpcProcess mcp;
    QEventLoop loop;
    QString error;
    QSet<QString> tools, cursors;
    const QSet<QString> supported{"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
    const auto quit = [&](const QString &message) {
        error = message;
        loop.quit();
    };
    const auto page = [&](const auto &self, const QString &cursor) -> void {
        QJsonObject params;
        if (!cursor.isEmpty()) {
            if (cursors.size() >= 20 || cursors.contains(cursor)) {
                quit("MCP 工具列表分页异常。");
                return;
            }
            cursors.insert(cursor);
            params.insert("cursor", cursor);
        }
        mcp.request("tools/list", params, [&, self](const QJsonValue &data, const RpcError &result) {
            const auto response = data.toObject();
            if (result || !response.value("tools").isArray()) {
                quit(result ? result.text : QString("MCP 工具列表格式无效。"));
                return;
            }
            for (const auto &item : response.value("tools").toArray()) {
                const auto tool = item.toObject();
                const auto name = tool.value("name").toString();
                if (!name.isEmpty())
                    tools.insert(name);
                if (name == "check_permissions") {
                    const auto schema = tool.value("inputSchema").toObject().value("properties").toObject();
                    report.readOnlyPermissionCheck = schema.value("prompt").toObject().value("type") == "boolean";
                }
            }
            const auto next = response.value("nextCursor").toString();
            if (!next.isEmpty())
                self(self, next);
            else
                loop.quit();
        }, 5000);
    };

    QObject::connect(&mcp, &RpcProcess::disconnected, &loop, quit);
    QObject::connect(&mcp, &RpcProcess::message, &loop, [&](const auto &message) {
        if (message.contains("id"))
            mcp.reject(message.value("id"));
    });
    QObject::connect(&mcp, &RpcProcess::started, &loop, [&] {
        const QJsonObject args{{"protocolVersion", "2025-11-25"}, {"capabilities", QJsonObject{}},
                               {"clientInfo", QJsonObject{{"name", "orbite"}, {"version", ORBITE_VERSION}}}};
        mcp.request("initialize", args, [&](const QJsonValue &data, const RpcError &result) {
            if (result) {
                quit(result.text);
                return;
            }
            const auto response = data.toObject();
            const auto protocol = response.value("protocolVersion").toString();
            if (!supported.contains(protocol) || !response.value("capabilities").toObject().value("tools").isObject()) {
                quit("MCP 握手未公布兼容的工具能力。");
                return;
            }
            report.protocol = protocol;
            mcp.notify("notifications/initialized", {});
            page(page, {});
        }, 5000);
    });
    QTimer::singleShot(15000, &loop, [&] { quit("MCP 诊断超时。"); });
    mcp.start(driver, {"mcp"}, QDir::homePath());
    loop.exec();
    if (!error.isEmpty())
        throw Failure(error);
    report.toolCount = tools.size();
    for (const auto &name : QStringList{"get_window_state", "list_windows", "click", "type_text"}) {
        if (!tools.contains(name))
            report.missingTools << name;
    }
}

}

DriverReport inspectDriver(const QString &driver)
{
    if (driver.isEmpty())
        throw Failure("未找到 cua-driver。可在设置中指定已安装的驱动。");
    DriverReport report;
    const auto version = runCommand(driver, {"--version"}, QDir::homePath(), 5000, 192 * 1024);
    static const QRegularExpression number(R"(\b[0-9]+\.[0-9]+\.[0-9]+(?:[-+.][\w.]+)?\b)");
    report.version = number.match(QString::fromUtf8(version.output)).captured();
    if (version.code || report.version.isEmpty())
        throw Failure("无法读取 Cua Driver 版本。");
    try {
        listTools(driver, report);
    } catch (const Failure &failure) {
        report.issues << failure.text;
    }
#ifdef Q_OS_MACOS
    try {
        const auto result = runCommand(driver, {"permissions", "status", "--json"}, QDir::homePath(), 9000, 192 * 1024);
        const auto response = QJsonDocument::fromJson(result.output).object();
        if (result.code || response.isEmpty()) {
            report.issues << "驱动未返回可读的 macOS 权限状态。";
        } else {
            report.permissions = response.value("permissions").toObject();
            if (report.permissions.isEmpty())
                report.permissions = response.value("result").toObject(response);
            report.permissionSource = report.permissions.value("source").toObject().value("attribution").toString().left(64);
        }
    } catch (const Failure &failure) {
        report.issues << failure.text;
    }
#else
    report.issues << "macOS 权限检查在当前平台不可用。";
#endif
    return report;
}

void driverSelfTest(const QString &driver)
{
#ifdef Q_OS_MACOS
    if (runCommand(driver, {"test", "--json"}, QDir::homePath(), 75000, 192 * 1024).code)
        throw Failure("Cua Driver 自测未通过。可在终端运行 cua-driver test 查看详细原因。");
#else
    Q_UNUSED(driver);
    throw Failure("桌面输入自测仅在 macOS 上可用。");
#endif
}

void driverAuthorize(const QString &driver)
{
#ifdef Q_OS_MACOS
    if (runCommand(driver, {"permissions", "grant"}, QDir::homePath(), 170000, 192 * 1024).code)
        throw Failure("授权流程未完成，请在系统设置中检查 CuaDriver。");
#else
    Q_UNUSED(driver);
    throw Failure("此授权流程仅在 macOS 上可用。");
#endif
}

}
