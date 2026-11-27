#pragma once

#include <QString>
#include <functional>

class QWidget;

namespace orbite {

void browseProjectFiles(QWidget *parent, const QString &root,
                        const std::function<void(const QString &, int)> &reference);

}
