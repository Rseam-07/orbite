#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QAbstractButton;
class QWidget;

namespace orbite {

struct Colors {
    QColor text;
    QColor muted;
    QColor accent;
    QColor tint;
    QColor userMessage;
    QColor added;
    QColor removed;
    QColor running;
};

enum class Icon { Home, Compose, Plus, Folder, Review, Chevron, Settings, Extension, Send, Stop, Attachment, Clock, Refresh, Close, Branch, Chat };

void applyTheme(const QString &choice);
Colors colors();
QIcon lineIcon(Icon icon, QColor color = {});
void setLineIcon(QAbstractButton *button, Icon icon);
void refreshLineIcons(QWidget *root);

}
