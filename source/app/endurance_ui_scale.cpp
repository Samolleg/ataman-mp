#include "endurance_ui_scale.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QWidget>
#include <QVector>
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
    const int previous = normalizedUiScalePercent(
        qApp->property("enduranceUiScalePercent").isValid()
            ? qApp->property("enduranceUiScalePercent").toInt() : 100);
    const int normalized = normalizedUiScalePercent(percent);
    const double liveRatio = double(normalized) / double(previous);
    QVector<QPair<QWidget*, QFont>> liveFonts;
    for(QWidget* window : QApplication::topLevelWidgets())
    {
        if(!window)
            continue;
        liveFonts.append(qMakePair(window, window->font()));
        const QList<QWidget*> children = window->findChildren<QWidget*>();
        for(QWidget* child : children)
            liveFonts.append(qMakePair(child, child->font()));
    }
    QFont scaled = baseFont;
    if(baseFont.pointSizeF() > 0.0)
        scaled.setPointSizeF(baseFont.pointSizeF() * normalized / 100.0);
    else if(baseFont.pixelSize() > 0)
        scaled.setPixelSize(qMax(1, qRound(baseFont.pixelSize() * normalized / 100.0)));
    QApplication::setFont(scaled);
    qApp->setProperty("enduranceUiScalePercent", normalized);

    // QApplication's default font reliably affects newly created popups, but
    // Qt 5 does not consistently propagate it into an already constructed
    // widget hierarchy (notably menu bars and designer-created dock content).
    // Scale snapshots of every live widget by the change ratio. This preserves
    // bold/italic families and deliberately larger or smaller child fonts.
    for(const auto& item : liveFonts)
    {
        QWidget* widget = item.first;
        if(!widget)
            continue;
        QFont font = item.second;
        if(font.pointSizeF() > 0.0)
            font.setPointSizeF(qMax(1.0, font.pointSizeF() * liveRatio));
        else if(font.pixelSize() > 0)
            font.setPixelSize(qMax(1, qRound(font.pixelSize() * liveRatio)));
        widget->setFont(font);
        widget->updateGeometry();
        widget->update();
    }
}

double uiScaleFactor()
{
    const QVariant configured = qApp
        ? qApp->property("enduranceUiScalePercent") : QVariant();
    return normalizedUiScalePercent(configured.isValid() ? configured.toInt() : 100)
        / 100.0;
}

}
