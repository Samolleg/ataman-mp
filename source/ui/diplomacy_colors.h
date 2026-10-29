#pragma once
#include <QColor>
#include <QVector4D>

namespace diplomacy {
inline QColor groupColor(int group)
{
    if(group < 0 || group >= 32) return QColor("#9aa7b5");
    // Golden-angle hues, alternating luminance, stable across maps and sessions.
    return QColor::fromHsv((group * 137 + 210) % 360,
                          group % 2 ? 165 : 205, group % 3 ? 235 : 255);
}
inline QColor relationColor(uint value)
{
    switch(value) {
    case 0: return QColor("#45d483");
    case 1: return QColor("#f2cc58");
    case 2: return QColor("#ef626c");
    default: return QColor("#9aa7b5");
    }
}
inline QVector4D mapTint(const QColor& color)
{
    return QVector4D(color.redF(), color.greenF(), color.blueF(), 0.9f);
}
}
