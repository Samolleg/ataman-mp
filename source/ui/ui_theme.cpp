#include "ui_theme.h"
#include <QApplication>
#include <QPalette>
#include <QTableWidgetItem>
namespace ei {
QColor uiTextColor(UiTextTone tone)
{
    switch(tone) {
    case UiTextTone::Error: return QColor(255, 140, 140);
    case UiTextTone::Warning: return QColor(255, 203, 107);
    case UiTextTone::Success: return QColor(132, 214, 145);
    case UiTextTone::Link: return QColor(129, 188, 255);
    case UiTextTone::Incomplete: return QColor(220, 157, 242);
    case UiTextTone::Muted: return QColor(184, 184, 184);
    }
    return Qt::white;
}

QString uiTextStyle(UiTextTone tone)
{
    return QStringLiteral("color: %1;").arg(uiTextColor(tone).name());
}

void applyRelationCellColors(QTableWidgetItem* cell, int value)
{
    // Shared by the relation editor and inspector. Neutral olive cells need black text.
    cell->setForeground(value == 1 ? Qt::black : Qt::white);
    switch (value) {
    case 0: cell->setBackground(Qt::darkGreen); break;
    case 1: cell->setBackground(Qt::darkYellow); break;
    case 2: cell->setBackground(Qt::darkRed); break;
    default: cell->setData(Qt::BackgroundRole, QVariant()); break;
    }
}

void installDarkUiPalette()
{
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53,53,53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25,25,25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53,53,53));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(250, 248, 240));
    darkPalette.setColor(QPalette::ToolTipText, QColor(32, 32, 32));
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53,53,53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, uiTextColor(UiTextTone::Error));
    darkPalette.setColor(QPalette::Link, uiTextColor(UiTextTone::Link));
    darkPalette.setColor(QPalette::LinkVisited, uiTextColor(UiTextTone::Incomplete));
    darkPalette.setColor(QPalette::Mid, uiTextColor(UiTextTone::Muted));
    darkPalette.setColor(QPalette::PlaceholderText, uiTextColor(UiTextTone::Muted));
    darkPalette.setColor(QPalette::Highlight, QColor(20, 208, 28));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    darkPalette.setColor(QPalette::Disabled, QPalette::Base, QColor(65, 65, 65));
    const QColor disabledText = uiTextColor(UiTextTone::Muted);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(65, 65, 65));

    qApp->setPalette(darkPalette);

    qApp->setStyleSheet(
        "QToolTip { color: #202020; background-color: #faf8f0; border: 1px solid #8a8a8a; }"
        "QToolButton:pressed { background-color: #3c583d; }"
        "QWidget:disabled { color: #b8b8b8; }"
        "QMenu::item:disabled { color: #b8b8b8; }");

}
}
