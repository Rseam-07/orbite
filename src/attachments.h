#pragma once

#include "common.h"

#include <atomic>

namespace orbite {

struct Attachment {
    QString name;
    QString path;
    bool directory = false;
    qint64 size = 0;
};

struct AttachmentBatch {
    QString directory;
    QList<Attachment> files;
    int skipped = 0;
};

Attachment describeAttachment(const QString &path);
AttachmentBatch stageAttachments(const QString &root, const QStringList &inputs,
                                 const std::atomic_bool *cancelled = nullptr);
QJsonArray promptContent(QString text, const QString &root, const AttachmentBatch &attachments,
                         bool allowImages);

}
