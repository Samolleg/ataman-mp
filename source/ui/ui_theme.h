#pragma once
#include <QColor>
#include <QString>
class QTableWidgetItem;
namespace ei {
enum class UiTextTone { Error, Warning, Success, Link, Incomplete, Muted };
QColor uiTextColor(UiTextTone tone);
QString uiTextStyle(UiTextTone tone);
void applyRelationCellColors(QTableWidgetItem* cell, int value);
void installDarkUiPalette();
}
