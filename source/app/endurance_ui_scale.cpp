#include "endurance_ui_scale.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QtMath>

namespace ei {

int normalizedUiScalePercent(int percent)
{
    return qBound(75, percent, 200);
}

int persistedUiScalePercent(const QString& optionsPath)
{
    QFile options(optionsPath);
    if(!options.open(QIODevice::ReadOnly | QIODevice::Text))
        return 100;

    const QJsonObject root = QJsonDocument::fromJson(options.readAll()).object();
    if(qAbs(root.value(QStringLiteral("Version")).toVariant().toDouble() - 1.0) > 0.001)
        return 100;
    const QJsonObject general = root.value(QStringLiteral("General")).toObject();
    return normalizedUiScalePercent(
        general.value(QStringLiteral("uiScalePercent")).toInt(100));
}

void applyUiScalePercent(int percent)
{
    if(!qApp)
        return;

    static const QFont baseFont = QApplication::font();
    const int normalized = normalizedUiScalePercent(percent);
    QFont scaled = baseFont;
    if(baseFont.pointSizeF() > 0.0)
        scaled.setPointSizeF(baseFont.pointSizeF() * normalized / 100.0);
    else if(baseFont.pixelSize() > 0)
        scaled.setPixelSize(qMax(1, qRound(baseFont.pixelSize() * normalized / 100.0)));
    QApplication::setFont(scaled);
    qApp->setProperty("enduranceUiScalePercent", normalized);
}

double uiScaleFactor()
{
    const QVariant configured = qApp
        ? qApp->property("enduranceUiScalePercent") : QVariant();
    return normalizedUiScalePercent(configured.isValid() ? configured.toInt() : 100)
        / 100.0;
}

}
