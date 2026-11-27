#pragma once

#include "common.h"

namespace orbite {

struct Scope {
    QString root;
    QString cwd;
    QString base;
    QString branch;
};

struct Change {
    QString path;
    QString status;
};

struct Review {
    QString branch;
    QList<Change> files;
};

struct Patch {
    QByteArray data;
    QStringList paths;
};

Scope openScope(const QString &root, const QString &profile, const QString &id,
                bool isolated, bool create);
Review readReview(const Scope &scope);
QString readPatch(const Scope &scope, const Change &file);
Patch buildPatch(const Scope &scope);
void checkApply(const QString &target, const Patch &patch);
void applyPatch(const QString &target, const Patch &patch);
bool pathsOverlap(const QString &a, const QString &b);
bool workspaceRelativePath(const QString &path);

}
