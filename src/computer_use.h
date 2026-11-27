#pragma once

#include "common.h"

namespace orbite {

struct DriverReport {
    QString version;
    QString protocol;
    int toolCount = 0;
    bool readOnlyPermissionCheck = false;
    QStringList missingTools;
    QJsonObject permissions;
    QString permissionSource;
    QStringList issues;
};

DriverReport inspectDriver(const QString &driver);
void driverSelfTest(const QString &driver);
void driverAuthorize(const QString &driver);

}
