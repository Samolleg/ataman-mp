#include "render_environment.h"

#include "types.h"

#include <QtMath>

namespace ei {

namespace {
float worldFloat(const CWorldSet* worldSet, EWsType type, float fallback)
{
    if(!worldSet)
        return fallback;
    bool ok = false;
    const float value = worldSet->data(type).toFloat(&ok);
    return ok && qIsFinite(value) ? value : fallback;
}

QVector3D lerp(const QVector3D& from, const QVector3D& to, float amount)
{
    return from * (1.0f - amount) + to * amount;
}

QVector3D cyclicHue(float hours)
{
    // The stock weather fields change continuously over the full clock cycle;
    // they do not gate the directional light at a horizon. These four keys are
    // a bounded editor approximation until provider weather-RGB curves are
    // exposed by the immutable database/resource service.
    static const QVector3D keys[] = {
        QVector3D(0.48f, 0.56f, 0.76f),
        QVector3D(0.92f, 0.70f, 0.48f),
        QVector3D(1.00f, 0.96f, 0.86f),
        QVector3D(0.92f, 0.64f, 0.43f),
        QVector3D(0.48f, 0.56f, 0.76f)
    };
    const float phase = hours / 6.0f;
    const int index = qBound(0, int(qFloor(phase)), 3);
    const float local = phase - float(index);
    const float smooth = local * local * (3.0f - 2.0f * local);
    return lerp(keys[index], keys[index + 1], smooth);
}
}

float clockValueToHours(int hourMinuteValue)
{
    const int bounded = qBound(0, hourMinuteValue, 2359);
    const int hours = bounded / 100;
    const int minutes = qMin(59, bounded % 100);
    return float(hours) + float(minutes) / 60.0f;
}

SRenderEnvironment renderEnvironmentForValues(float timeHours,
                                               float ambientStrength,
                                               float sunStrength)
{
    SRenderEnvironment result;
    result.timeHours = timeHours;
    result.timeHours = std::fmod(std::fmod(result.timeHours, 24.0f) + 24.0f, 24.0f);
    result.ambientStrength = qBound(0.0f, ambientStrength, 2.0f);
    result.sunStrength = qBound(0.0f, sunStrength, 4.0f);

    // EI rotates a slanted light vector around the map. It neither crosses a
    // horizon nor reaches a vertical zenith, so light strength never vanishes
    // merely because the clock advances.
    result.skyRotationDegrees = result.timeHours * 15.0f;
    const float azimuth = qDegreesToRadians(result.skyRotationDegrees + 35.0f);
    const float vertical = 0.62f;
    const float horizontal = qSqrt(1.0f - vertical * vertical);
    const QVector3D fromSurface(horizontal * qCos(azimuth),
                                horizontal * qSin(azimuth), vertical);
    result.sunDirection = -fromSurface.normalized();

    result.sunColor = cyclicHue(result.timeHours);
    result.ambientColor = lerp(QVector3D(0.52f, 0.58f, 0.68f),
                               result.sunColor, 0.35f);
    result.fogColor = lerp(QVector3D(0.46f, 0.52f, 0.60f),
                           result.sunColor, 0.28f);
    // The stock sky cylinder is open below its horizon. Distant landscape must
    // reach the environment color before the finite editor map edge exposes
    // that opening.
    result.fogStart = 180.0f;
    result.fogEnd = 620.0f;
    return result;
}

SRenderEnvironment renderEnvironment(const CWorldSet* worldSet,
                                     bool overrideTime, float overrideHours)
{
    SRenderEnvironment defaults;
    return renderEnvironmentForValues(
        overrideTime ? overrideHours
                     : worldFloat(worldSet, eWsTypeTime, defaults.timeHours),
        worldFloat(worldSet, eWsTypeAmbient, defaults.ambientStrength),
        worldFloat(worldSet, eWsTypeSunLight, defaults.sunStrength));
}

}
