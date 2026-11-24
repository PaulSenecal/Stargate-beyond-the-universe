#ifndef MENUSTYLE_H
#define MENUSTYLE_H

#include <QSizePolicy>
#include <QStackedWidget>
#include <QString>

// ================================================================
//  Shared look of the menus (title screen, pause menu, settings):
//  dark panels with the gold of the in-game HUD.
// ================================================================

namespace MenuStyle {

inline QString sheet()
{
    return QStringLiteral(
        "QLabel { color: #e8e2c8; }"
        "QLabel#title { color: #e2bf3e; font-size: 30px; font-weight: bold; letter-spacing: 4px; }"
        "QLabel#subtitle { color: #a9a28a; font-size: 14px; letter-spacing: 2px; }"
        "QLabel#heading { color: #e2bf3e; font-size: 20px; font-weight: bold; letter-spacing: 3px; }"
        "QLabel#note { color: #a9a28a; font-size: 12px; }"
        "QLabel#field { font-size: 14px; }"
        "QFrame#panel { background: rgba(20, 19, 14, 235); border: 1px solid #8a8160; border-radius: 6px; }"
        "QPushButton { background: #1c1a12; color: #e8e2c8; border: 1px solid #5c5642; border-radius: 4px;"
        "              padding: 10px 28px; font-size: 15px; letter-spacing: 1px; min-width: 220px; }"
        "QPushButton:hover, QPushButton:focus { border-color: #e2bf3e; color: #e2bf3e; }"
        "QPushButton:pressed { background: #e2bf3e; color: #111; }"
        "QPushButton#danger:hover, QPushButton#danger:focus { border-color: #ff5a48; color: #ff5a48; }"
        "QCheckBox { color: #e8e2c8; font-size: 14px; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #8a8160; background: #1c1a12; }"
        "QCheckBox::indicator:checked { background: #e2bf3e; }"
        "QComboBox { background: #1c1a12; color: #e8e2c8; border: 1px solid #5c5642; border-radius: 4px;"
        "            padding: 5px 10px; font-size: 14px; min-width: 140px; }"
        "QComboBox:hover, QComboBox:focus { border-color: #e2bf3e; }"
        "QComboBox QAbstractItemView { background: #1c1a12; color: #e8e2c8; selection-background-color: #e2bf3e;"
        "                              selection-color: #111; border: 1px solid #8a8160; }"
        "QSlider::groove:horizontal { height: 6px; background: #3a3626; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #e2bf3e; border-radius: 3px; }"
        "QSlider::handle:horizontal { width: 14px; margin: -5px 0; background: #e8e2c8; border-radius: 7px; }");
}

// Shows a page of a menu; only that page counts in the menu size
// (a QStackedWidget is otherwise as big as its biggest page)
inline void showPage(QStackedWidget *pages, int index)
{
    for (int i = 0; i < pages->count(); ++i) {
        const QSizePolicy::Policy policy = i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        pages->widget(i)->setSizePolicy(policy, policy);
    }
    pages->setCurrentIndex(index);
    pages->adjustSize();
    if (QWidget *panel = pages->parentWidget())
        panel->adjustSize();
}

} // namespace MenuStyle

#endif // MENUSTYLE_H
