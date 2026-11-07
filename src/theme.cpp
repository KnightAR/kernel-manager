// SPDX-License-Identifier: GPL-2.0-only
#include "theme.h"
#include <QApplication>
#include <QPalette>
#include <QFont>
#include <QStyleFactory>

void applyTheme(const QString &name, int fontSize) {
    QString window, base, alternate, text, muted, button, border, accent, selection, selected, link, header, sidebar, sidebarAlternate;
    if (name == "Paper") {
        window="#e9f0fa"; base="#ffffff"; alternate="#edf2f9"; text="#17283f"; muted="#50637f";
        button="#f6f9ff"; border="#c5d3e6"; accent="#315dce"; selection="#315dce"; selected="#ffffff"; link="#315dce"; header="#edf3fc";
        sidebar="#f7faff"; sidebarAlternate="#edf2f9";
    } else if (name == "High contrast") {
        window="#000000"; base="#000000"; alternate="#181818"; text="#ffffff"; muted="#c5c5c5";
        button="#101010"; border="#eeeeee"; accent="#ffed80"; selection="#ffed80"; selected="#000000"; link="#94d5ff"; header="#292929";
    } else if (name == "Menuconfig") {
        window="#080d63"; base="#101d76"; alternate="#192b8e"; text="#ffffff"; muted="#ccd8ff";
        button="#243888"; border="#a3b5ff"; accent="#ffed85"; selection="#ccecff"; selected="#08113c"; link="#adf2ff"; header="#293e9c";
    } else {
        window="#111824"; base="#192333"; alternate="#223047"; text="#f1f4fb"; muted="#b9c6da";
        button="#293b54"; border="#8193ad"; accent="#d7c6ff"; selection="#446691"; selected="#ffffff"; link="#aad2ff"; header="#34465e";
    }
    if (sidebar.isEmpty()) { sidebar=window; sidebarAlternate=alternate; }
    const QString rail = name == "Paper" ? "#263240" : window;
    const QString railText = name == "High contrast" ? "#ffffff" : "#dae7fa";
    QPalette palette;
    palette.setColor(QPalette::Window,QColor(window)); palette.setColor(QPalette::WindowText,QColor(text));
    palette.setColor(QPalette::Base,QColor(base)); palette.setColor(QPalette::AlternateBase,QColor(alternate));
    palette.setColor(QPalette::Text,QColor(text)); palette.setColor(QPalette::Button,QColor(button));
    palette.setColor(QPalette::ButtonText,QColor(text)); palette.setColor(QPalette::Highlight,QColor(selection));
    palette.setColor(QPalette::HighlightedText,QColor(selected)); palette.setColor(QPalette::Link,QColor(link));
    palette.setColor(QPalette::Mid,QColor(border));
    palette.setColor(QPalette::PlaceholderText,QColor(muted));
    palette.setColor(QPalette::ToolTipBase,QColor(base)); palette.setColor(QPalette::ToolTipText,QColor(text));
    palette.setColor(QPalette::Disabled,QPalette::Text,QColor(muted)); palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor(muted));
    qApp->setPalette(palette);
    qApp->setFont(QFont(name == "Menuconfig" ? "Noto Sans Mono" : "Noto Sans", fontSize));
    QString style = R"(
        QWidget#workspace { background: WINDOW; }
        QWidget#windowHeader { background: RAIL; border-bottom: 1px solid BORDER; }
        QWidget#navigationRail { background: RAIL; }
        QToolButton#railButton { color: RAILTEXT; background: transparent; text-align: left; border: 1px solid transparent; padding: 12px 8px; border-radius: 7px; }
        QToolButton#railButton:hover { background: #35465d; }
        QToolButton#railButton:checked { background: SELECTION; color: SELECTED; border-color: ACCENT; font-weight: 600; }
        QToolButton#railButton:focus { border-color: RAILTEXT; }
        QLabel#railSummary { color: RAILTEXT; background: RAIL; border: 1px solid #52637a; border-radius: 8px; padding: 16px 12px; }
        QWidget#contentPane { background: BASE; border: 1px solid BORDER; border-radius: 10px; }
        QWidget#browserCard { background: SIDEBAR; border: 1px solid BORDER; border-radius: 10px; }
        QTreeView#kernelMenu { background: SIDEBAR; alternate-background-color: SIDEALTERNATE; border: 0; color: TEXT; outline: 0; }
        QTreeView#kernelMenu:focus { border: 1px solid ACCENT; }
        QTabWidget#navigation::pane { background: SIDEBAR; border: 0; }
        QMenuBar { background: transparent; color: RAILTEXT; padding: 0; }
        QMenuBar::item { background: transparent; padding: 7px 12px; border-radius: 4px; }
        QMenuBar::item:selected, QMenuBar::item:pressed { background: SELECTION; color: SELECTED; }
        QMenu { background: BASE; color: TEXT; border: 1px solid BORDER; padding: 5px; }
        QMenu::item { padding: 7px 28px; border-radius: 3px; }
        QMenu::item:selected { background: SELECTION; color: SELECTED; }
        QMenu::item:disabled { color: MUTED; }
        QMenu::separator { height: 1px; background: BORDER; margin: 5px 8px; }
        QToolButton, QPushButton { background: BUTTON; color: TEXT; padding: 8px 10px; border: 1px solid BORDER; border-radius: 6px; }
        QPushButton:hover, QToolButton:hover { background: HEADER; }
        QPushButton:checked { background: SELECTION; color: SELECTED; border: 1px solid ACCENT; }
        QPushButton:disabled, QToolButton:disabled { color: MUTED; }
        QPushButton:focus, QToolButton:focus, QLineEdit:focus, QComboBox:focus, QTreeWidget:focus, QTableWidget:focus { border: 2px solid ACCENT; }
        QLineEdit, QComboBox, QSpinBox { background: BUTTON; color: TEXT; padding: 7px; border: 1px solid BORDER; border-radius: 6px; }
        QLineEdit#globalSearch { background: #e5ecf7; color: #20344f; padding: 6px; selection-background-color: #315dce; selection-color: white; }
        QLineEdit QToolButton { background: transparent; border: 0; border-radius: 0; padding: 0; }
        QLabel#heading { font-size: HEADSIZEpt; font-weight: 700; color: TEXT; }
        QLabel#pageHeading { font-size: PAGESIZEpt; font-weight: 700; color: TEXT; }
        QLabel#symbol { color: MUTED; font-family: monospace; }
        QLabel#coverage { color: LINK; font-weight: 600; padding: 6px 0; }
        QLabel#supportBadge { color: LINK; background: HEADER; border: 1px solid BORDER; border-radius: 6px; padding: 6px 8px; }
        QLabel#muted { color: MUTED; }
        QTabWidget::pane { border: 1px solid BORDER; border-radius: 5px; }
        QTabWidget#detailTabs::pane { background: BUTTON; border: 0; }
        QTabBar::tab { background: transparent; padding: 9px 8px; color: TEXT; border: 0; border-bottom: 2px solid transparent; margin: 0 2px 0 0; }
        QTabBar::tab:hover { background: HEADER; color: TEXT; border-top-left-radius: 6px; border-top-right-radius: 6px; }
        QTabBar::tab:selected { background: HEADER; color: LINK; border-bottom-color: ACCENT; border-top-left-radius: 6px; border-top-right-radius: 6px; }
        QTabBar QToolButton { background: BUTTON; border: 1px solid BORDER; padding: 0; border-radius: 4px; }
        QTabBar::scroller { width: 32px; }
        QTreeView::item { min-height: ROWHEIGHTpx; padding: 3px 1px; }
        QHeaderView::section { padding: 8px 7px; background: HEADER; color: TEXT; font-weight: 600; border: 0; }
        QTextBrowser { padding: 8px; border: 0; }
        QSplitter::handle { background: transparent; width: 12px; height: 12px; }
        QSplitter::handle:hover { background: HEADER; }
        QStatusBar { color: TEXT; padding: 3px 7px; }
        QListWidget::item { padding: 11px; }
    )";
    {
        // Preserve the palette's RGB values; change only background alpha.
        auto alpha = [](const QString &color, int opacity) {
            const QColor c(color);
            return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(opacity);
        };
        style += QString(R"(
            QWidget#workspace { background: %1; }
            QWidget#windowHeader, QWidget#navigationRail { background: %2; }
            QLabel#railSummary { background: transparent; }
            QWidget#browserCard { background: %3; }
            QWidget#contentPane { background: %4; }
            QTreeView#kernelMenu { background: transparent; alternate-background-color: %5; }
            QTabWidget#navigation::pane, QTabWidget#detailTabs::pane { background: transparent; }
            QTabWidget#detailTabs QTextBrowser, QTabWidget#detailTabs QAbstractItemView { background: transparent; alternate-background-color: %5; }
            QStatusBar { background: %1; }
        )").arg(alpha(window,32),alpha(rail,184),alpha(sidebar,96),alpha(base,96),alpha(alternate,30));
    }
    for (auto pair : {qMakePair(QString("BORDER"),border), qMakePair(QString("RAILTEXT"),railText), qMakePair(QString("RAIL"),rail),
                     qMakePair(QString("WINDOW"),window), qMakePair(QString("BUTTON"),button), qMakePair(QString("LINK"),link), qMakePair(QString("ACCENT"),accent),
                     qMakePair(QString("HEADER"),header), qMakePair(QString("SELECTION"),selection),
                     qMakePair(QString("SELECTED"),selected), qMakePair(QString("TEXT"),text), qMakePair(QString("MUTED"),muted),
                     qMakePair(QString("BASE"),base), qMakePair(QString("SIDEBAR"),sidebar), qMakePair(QString("SIDEALTERNATE"),sidebarAlternate)}) style.replace(pair.first,pair.second);
    style.replace("PAGESIZE",QString::number(fontSize+3)); style.replace("HEADSIZE",QString::number(fontSize+5)); style.replace("ROWHEIGHT",QString::number(fontSize+10));
    qApp->setStyleSheet(style);
}
