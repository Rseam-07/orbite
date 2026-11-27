#include "theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleHints>

namespace orbite {
namespace {

bool isDark = false;

}

Colors colors()
{
    if (isDark)
        return {"#ecedf2", "#a1a8ba", "#b4c5f2", "#2a3549", "#242a37",
                "#a9d8b5", "#f1afac", "#bccef9"};
    return {"#292c27", "#646860", "#3c6559", "#e4ece4", "#eeede6",
            "#386748", "#a24743", "#3f6b60"};
}

QIcon lineIcon(Icon icon, QColor color)
{
    if (!color.isValid())
        color = colors().muted;
    QPixmap pixmap(36, 36);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const auto path = [&](std::initializer_list<QPointF> points) {
        QPainterPath shape;
        shape.moveTo(*points.begin());
        for (auto p = points.begin() + 1; p != points.end(); ++p)
            shape.lineTo(*p);
        painter.drawPath(shape);
    };
    switch (icon) {
    case Icon::Home:
        for (int x : {3, 10}) {
            for (int y : {3, 10})
                painter.drawRoundedRect(QRectF(x, y, 4.7, 4.7), 0.8, 0.8);
        }
        break;
    case Icon::Compose:
        path({{12, 4}, {4, 4}, {4, 14}, {14, 14}, {14, 8}});
        path({{8, 10}, {14, 4}, {15, 5}, {9, 11}, {8, 11}, {8, 10}});
        break;
    case Icon::Plus:
        painter.drawLine(QPointF(9, 4), QPointF(9, 14));
        painter.drawLine(QPointF(4, 9), QPointF(14, 9));
        break;
    case Icon::Folder:
        path({{2.7, 6}, {2.7, 4.5}, {7.2, 4.5}, {9, 6.2}, {15.3, 6.2},
              {15.3, 13.4}, {2.7, 13.4}, {2.7, 6}});
        break;
    case Icon::Review:
        painter.drawRoundedRect(QRectF(2.7, 3.8, 12.6, 10.4), 1.5, 1.5);
        painter.drawLine(QPointF(10.7, 4), QPointF(10.7, 14));
        break;
    case Icon::Chevron:
        path({{7.5, 5}, {11.5, 9}, {7.5, 13}});
        break;
    case Icon::Settings:
        for (int y : {5, 9, 13})
            painter.drawLine(QPointF(3, y), QPointF(15, y));
        for (const auto &p : {QPointF(7, 5), QPointF(11, 9), QPointF(8, 13)}) {
            painter.setBrush(color);
            painter.drawEllipse(p, 1.1, 1.1);
        }
        break;
    case Icon::Extension:
        painter.drawRoundedRect(QRectF(3.5, 3.5, 11, 11), 2, 2);
        painter.drawLine(QPointF(7, 9), QPointF(11, 9));
        painter.drawLine(QPointF(9, 7), QPointF(9, 11));
        break;
    case Icon::Send:
        path({{4.9, 8}, {9, 3.9}, {13.1, 8}});
        painter.drawLine(QPointF(9, 4), QPointF(9, 14.5));
        break;
    case Icon::Stop:
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(5, 5, 8, 8), 1, 1);
        break;
    case Icon::Attachment:
        painter.drawArc(QRectF(5, 2.8, 8, 11.7), 80 * 16, 278 * 16);
        path({{13, 8}, {13, 11}, {11.3, 13.4}, {8.4, 13.4}, {7.6, 11.9}, {7.6, 6.6}});
        break;
    case Icon::Clock:
        painter.drawEllipse(QRectF(3, 3, 12, 12));
        path({{9, 5.6}, {9, 9}, {11.6, 10.5}});
        break;
    case Icon::Refresh:
        painter.drawArc(QRectF(3.7, 3.7, 10.6, 10.6), 27 * 16, 290 * 16);
        path({{14, 3.5}, {14, 7.6}, {10.5, 7.6}});
        break;
    case Icon::Close:
        painter.drawLine(QPointF(5, 5), QPointF(13, 13));
        painter.drawLine(QPointF(13, 5), QPointF(5, 13));
        break;
    case Icon::Branch:
        painter.drawEllipse(QPointF(6, 4.5), 1.4, 1.4);
        painter.drawEllipse(QPointF(6, 13.4), 1.4, 1.4);
        painter.drawEllipse(QPointF(12.4, 6.4), 1.4, 1.4);
        painter.drawLine(QPointF(6, 6), QPointF(6, 12));
        path({{6, 10}, {10.4, 10}, {12.4, 8}});
        break;
    case Icon::Chat:
        path({{3.5, 4.7}, {14.5, 4.7}, {14.5, 11.7}, {8.5, 11.7},
              {5.8, 14}, {5.8, 11.7}, {3.5, 11.7}, {3.5, 4.7}});
        break;
    }
    painter.end();
    return QIcon(pixmap);
}

void setLineIcon(QAbstractButton *button, Icon icon)
{
    button->setProperty("lineIcon", static_cast<int>(icon));
    button->setIcon(lineIcon(icon));
    button->setIconSize(QSize(16, 16));
}

void refreshLineIcons(QWidget *root)
{
    for (auto *button : root->findChildren<QAbstractButton *>()) {
        const auto tag = button->property("lineIcon");
        if (tag.isValid())
            button->setIcon(lineIcon(static_cast<Icon>(tag.toInt())));
    }
}

void applyTheme(const QString &choice)
{
    isDark = choice == "dark" || (choice != "light" && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    const auto c = colors();
    const QString bg = isDark ? "#171921" : "#f8f7f3";
    const QString rail = isDark ? "#1d202a" : "#eeede7";
    const QString panel = isDark ? "#1c1f2a" : "#f3f2ed";
    const QString raised = isDark ? "#222632" : "#fffefa";
    const QString line = isDark ? "#323746" : "#dfded6";
    const QString soft = isDark ? "#272c39" : "#f1f0ea";
    const QString hover = isDark ? "#2a2f3e" : "#e9e8e1";
    const QString selected = isDark ? "#2a3549" : "#e1e8df";
    const QString hot = isDark ? "#b4c7ff" : "#3e6a5c";
    const QString hotHover = isDark ? "#c6d4ff" : "#34594e";
    const QString hotText = isDark ? "#172033" : "#ffffff";
    const QString approval = isDark ? "#f2c59e" : "#9c6548";
    const QString scroll = isDark ? "#404858" : "#cdcdc4";
    const QString disabled = isDark ? "#697286" : "#92948d";

    QPalette palette;
    palette.setColor(QPalette::Window, bg);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, raised);
    palette.setColor(QPalette::AlternateBase, panel);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, raised);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::Highlight, selected);
    palette.setColor(QPalette::HighlightedText, c.text);
    palette.setColor(QPalette::PlaceholderText, c.muted);
    palette.setColor(QPalette::ToolTipBase, raised);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    qApp->setPalette(palette);

    QString sheet = R"(
        QWidget { color: @text@; }
        QMainWindow, #conversation, #workbench, #workbenchScroll { background: @bg@; }
        #rail { background: @rail@; }
        #reviewPanel { background: @panel@; }
        #brand { font-size: 18px; font-weight: 700; }
        #railLabel, #caption, #reviewSummary { font-size: 11px; font-weight: 600; color: @muted@; }
        #threadTitle { font-size: 20px; font-weight: 650; }
        #homeTitle, #sessionWelcomeTitle { font-size: 32px; font-weight: 680; }
        #sectionTitle { font-size: 14px; font-weight: 650; }
        #homeProjectName { font-size: 14px; font-weight: 650; }
        #homeRecentName { font-size: 13px; font-weight: 570; }
        #homeMuted, #sessionWelcomeText, #composerFootnote { font-size: 12px; color: @muted@; }
        #homePath { font-size: 11px; color: @muted@; }
        #homeTile { background: @tint@; border-radius: 9px; }
        #homeTable { background: @raised@; border: 1px solid @line@; border-radius: 11px; }
        #homeRule, #railRule { background: @line@; }
        QPushButton[homeRow="true"] {
            background: transparent; border: 0; padding: 0; border-radius: 6px;
            text-align: left;
        }
        QPushButton[homeRow="true"]:hover, QPushButton[homeRow="true"]:focus { background: @soft@; }
        #reviewTitle { font-size: 17px; font-weight: 650; }
        #projectHeader { border-bottom: 1px solid @line@; }
        #composerCard { background: @raised@; border: 1px solid @line@; border-radius: 15px; }
        #composerCard[focused="true"] { border-color: @accent@; }
        #composerEdit {
            background: transparent; border: 0; padding: 4px 6px; font-size: 15px;
            selection-background-color: @selected@; selection-color: @text@;
        }
        #approvalCard { background: @soft@; border: 1px solid @approval@; border-radius: 10px; }
        #queueCard { background: @soft@; border: 1px solid @line@; border-radius: 9px; }
        #status, #engineStatus { color: @muted@; font-size: 11px; }
        #engineStatus[state="active"] { color: @accent@; }
        #engineStatus[state="approval"] { color: @approval@; }
        #transcript { border: 0; background: @bg@; }
        #gitPatch, #sessionDiff, #activityText, #permissionBody {
            background: @raised@; border: 1px solid @line@; border-radius: 8px; padding: 6px;
        }
        QPushButton, QToolButton, QComboBox {
            background: @raised@; border: 1px solid @line@; border-radius: 7px;
            min-height: 18px; padding: 5px 10px; font-size: 12px;
        }
        QPushButton:hover, QToolButton:hover, QComboBox:hover { background: @hover@; }
        QPushButton:checked, QToolButton:checked { background: @selected@; color: @accent@; }
        QPushButton:focus, QToolButton:focus, QComboBox:focus, QLineEdit:focus { border: 1px solid @accent@; }
        QPushButton:disabled, QToolButton:disabled, QComboBox:disabled { color: @disabled@; }
        QPushButton[primary="true"] {
            background: @hot@; color: @hotText@; border-color: @hot@; font-weight: 650;
        }
        QPushButton[primary="true"]:hover { background: @hotHover@; border-color: @hotHover@; }
        QPushButton[primary="true"]:disabled { background: @soft@; color: @disabled@; border-color: @soft@; }
        QPushButton[ghost="true"], QToolButton[ghost="true"] {
            background: transparent; border: 1px solid transparent; font-weight: 550;
        }
        QPushButton[ghost="true"]:hover, QToolButton[ghost="true"]:hover { background: @hover@; }
        QPushButton[ghost="true"]:checked { background: @selected@; color: @accent@; }
        QPushButton[ghost="true"]:focus, QToolButton[ghost="true"]:focus { border-color: @accent@; }
        QPushButton[railNav="true"] { text-align: left; min-height: 22px; padding: 5px 7px; }
        QPushButton[railNav="true"]:checked:focus { border-color: @line@; }
        QPushButton[pill="true"], QToolButton[pill="true"] { border-radius: 12px; min-height: 18px; padding: 3px 9px; }
        QPushButton[pill="true"]:checked { border-color: @line@; border-radius: 12px; }
        QPushButton[small="true"] { padding: 2px 7px; min-height: 18px; }
        #sendPrompt { border-radius: 9px; padding: 4px 9px; min-height: 20px; }
        #projectSearch, QLineEdit {
            background: @raised@; border: 1px solid @line@; border-radius: 7px;
            padding: 6px 8px; font-size: 12px;
        }
        QTreeWidget, QListWidget, QScrollArea { border: 0; background: transparent; outline: 0; }
        QTreeWidget::item, QListWidget::item { min-height: 29px; padding: 2px 4px; }
        QTreeWidget::item:selected, QListWidget::item:selected { background: @selected@; color: @text@; }
        QTreeWidget::item:hover, QListWidget::item:hover { background: @hover@; }
        QTreeWidget::branch:selected { background: @selected@; }
        #archiveFilter { color: @muted@; font-size: 11px; }
        QTabWidget::pane { border: 0; }
        QTabBar::tab {
            padding: 7px 9px; color: @muted@; border-bottom: 2px solid @line@; font-size: 11px;
        }
        QTabBar::tab:selected { color: @accent@; border-bottom: 2px solid @accent@; }
        QSplitter::handle { background: @line@; }
        QStatusBar { background: @bg@; color: @muted@; font-size: 11px; }
        QMenu { background: @raised@; border: 1px solid @line@; padding: 4px; }
        QMenu::item { padding: 6px 19px 6px 10px; }
        QMenu::item:selected { background: @selected@; color: @text@; }
        QScrollBar:vertical { background: transparent; width: 7px; margin: 2px 0; }
        QScrollBar::handle:vertical { background: @scroll@; min-height: 26px; border-radius: 3px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QSlider::groove:horizontal { height: 4px; background: @line@; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: @accent@; border-radius: 2px; }
        QSlider::handle:horizontal { width: 13px; margin: -5px 0; border-radius: 7px; background: @hot@; }
    )";
    const QHash<QString, QString> values{{"text", c.text.name()}, {"muted", c.muted.name()}, {"accent", c.accent.name()},
                                        {"tint", c.tint.name()}, {"bg", bg}, {"rail", rail}, {"panel", panel},
                                        {"raised", raised}, {"line", line}, {"soft", soft}, {"hover", hover},
                                        {"selected", selected}, {"hot", hot}, {"hotHover", hotHover},
                                        {"hotText", hotText}, {"approval", approval}, {"scroll", scroll},
                                        {"disabled", disabled}};
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        sheet.replace("@" + it.key() + "@", it.value());
    qApp->setStyleSheet(sheet);
}

}
