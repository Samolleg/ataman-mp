#pragma once
#include <QColor>
#include <QString>
namespace ei {
enum class UiTextTone { Error, Warning, Success, Link, Incomplete, Muted };
QColor uiTextColor(UiTextTone tone);
QString uiTextStyle(UiTextTone tone);
void installDarkUiPalette();
}
