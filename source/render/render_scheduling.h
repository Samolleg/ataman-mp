#ifndef RENDER_SCHEDULING_H
#define RENDER_SCHEDULING_H

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <QtGlobal>

namespace ei {

inline int renderTimerIntervalMs(int fpsCap)
{
    if(fpsCap <= 0)
        return 0;
    return qMax(1, qCeil(1000.0 / qBound(1, fpsCap, 1000)));
}

inline int renderSwapInterval(bool vsyncEnabled, bool benchmarkMode)
{
    return benchmarkMode ? 0 : (vsyncEnabled ? 1 : 0);
}

inline bool renderVsyncFromOptions(const QByteArray& json, bool defaultValue = true)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if(parseError.error != QJsonParseError::NoError || !document.isObject())
        return defaultValue;
    const QJsonValue value = document.object().value("Render").toObject().value("renderVSync");
    return value.isBool() ? value.toBool() : defaultValue;
}

}

#endif // RENDER_SCHEDULING_H
