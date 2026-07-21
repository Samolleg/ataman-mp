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

float smoothStep(float edge0, float edge1, float value)
{
    const float t = qBound(0.0f, (value - edge0) / (edge1 - edge0), 1.0f);
    return t * t * (3.0f - 2.0f * t);
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

    // MOB time is stored in hours. The preview follows the conventional EI
    // day arc: sunrise at 06:00, zenith at 12:00, sunset at 18:00. The MOB's
    // independent ambient/sun fields remain the authoritative intensities.
    const float dayAngle = qDegreesToRadians((result.timeHours - 6.0f) * 15.0f);
    const float elevation = qSin(dayAngle);
    const float azimuth = qDegreesToRadians(result.timeHours * 15.0f + 35.0f);
    const float horizontal = qSqrt(qMax(0.0f, 1.0f - elevation * elevation));
    const QVector3D fromSurface(horizontal * qCos(azimuth),
                                horizontal * qSin(azimuth), elevation);
    result.sunDirection = -fromSurface.normalized();

    const float daylight = smoothStep(-0.12f, 0.28f, elevation);
    const QVector3D nightColor(0.22f, 0.29f, 0.48f);
    const QVector3D dayColor(1.0f, 0.96f, 0.86f);
    result.sunColor = nightColor * (1.0f - daylight) + dayColor * daylight;
    result.sunStrength *= daylight;
    const QVector3D nightAmbient(0.20f, 0.25f, 0.38f);
    const QVector3D dayAmbient(0.72f, 0.78f, 0.84f);
    result.ambientColor = nightAmbient * (1.0f - daylight)
                        + dayAmbient * daylight;
    const QVector3D nightFog(0.055f, 0.075f, 0.13f);
    const QVector3D dayFog(0.58f, 0.66f, 0.72f);
    result.fogColor = nightFog * (1.0f - daylight) + dayFog * daylight;
    result.fogStart = 180.0f + 70.0f * daylight;
    result.fogEnd = 650.0f + 250.0f * daylight;
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
