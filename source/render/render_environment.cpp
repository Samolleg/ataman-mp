#include "render_environment.h"

#include "types.h"

#include <QtMath>

#include <array>

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

using HourlyColors = std::array<QVector3D, 24>;

QVector3D rgb(int red, int green, int blue)
{
    return QVector3D(float(red), float(green), float(blue)) / 255.0f;
}

const HourlyColors& gipatSunlight()
{
    static const HourlyColors values{{
        rgb(63,93,157), rgb(63,93,157), rgb(92,122,165), rgb(171,131,119),
        rgb(255,140,104), rgb(255,198,129), rgb(255,248,207), rgb(255,255,255),
        rgb(255,255,255), rgb(255,255,255), rgb(255,255,255), rgb(255,255,255),
        rgb(255,255,255), rgb(255,255,255), rgb(255,255,255), rgb(255,255,255),
        rgb(255,255,255), rgb(255,255,255), rgb(255,248,207), rgb(255,202,136),
        rgb(255,123,82), rgb(166,128,116), rgb(92,122,165), rgb(63,93,157)
    }};
    return values;
}

const HourlyColors& gipatAmbient()
{
    static const HourlyColors values{{
        rgb(54,83,147), rgb(54,83,147), rgb(87,89,110), rgb(122,81,68),
        rgb(133,96,85), rgb(135,117,94), rgb(129,135,125), rgb(129,135,125),
        rgb(129,135,125), rgb(129,135,125), rgb(129,135,125), rgb(129,135,125),
        rgb(129,135,125), rgb(129,135,125), rgb(129,135,125), rgb(129,135,125),
        rgb(129,135,125), rgb(129,135,125), rgb(129,135,125), rgb(135,117,94),
        rgb(133,96,85), rgb(122,81,68), rgb(87,89,110), rgb(54,83,147)
    }};
    return values;
}

const HourlyColors& ingosAmbient()
{
    static const HourlyColors values{{
        rgb(54,83,147), rgb(54,83,147), rgb(77,87,135), rgb(88,94,133),
        rgb(99,104,133), rgb(108,115,138), rgb(111,121,151), rgb(111,121,151),
        rgb(111,121,151), rgb(111,121,151), rgb(111,121,151), rgb(111,121,151),
        rgb(111,121,151), rgb(111,121,151), rgb(111,121,151), rgb(111,121,151),
        rgb(111,121,151), rgb(111,121,151), rgb(111,121,151), rgb(108,115,138),
        rgb(99,104,133), rgb(88,94,133), rgb(77,87,135), rgb(54,83,147)
    }};
    return values;
}

const HourlyColors& gipatSky()
{
    static const HourlyColors values{{
        rgb(0,0,118), rgb(0,0,118), rgb(76,78,97), rgb(151,80,58),
        rgb(196,89,56), rgb(175,150,126), rgb(138,176,177), rgb(53,182,198),
        rgb(46,181,217), rgb(46,181,217), rgb(46,181,217), rgb(46,181,217),
        rgb(46,181,217), rgb(46,181,217), rgb(46,181,217), rgb(46,181,217),
        rgb(46,181,217), rgb(103,181,183), rgb(174,168,157), rgb(210,154,101),
        rgb(210,95,36), rgb(136,81,81), rgb(76,78,97), rgb(0,0,118)
    }};
    return values;
}

const HourlyColors& ingosSky()
{
    static const HourlyColors values{{
        rgb(0,0,118), rgb(0,0,118), rgb(75,83,95), rgb(151,80,58),
        rgb(196,89,56), rgb(175,150,126), rgb(138,167,177), rgb(53,157,198),
        rgb(46,132,217), rgb(46,132,217), rgb(46,132,217), rgb(46,132,217),
        rgb(46,132,217), rgb(46,132,217), rgb(46,132,217), rgb(46,132,217),
        rgb(53,157,198), rgb(103,168,183), rgb(174,168,157), rgb(210,154,101),
        rgb(210,95,36), rgb(136,81,81), rgb(75,83,95), rgb(0,0,118)
    }};
    return values;
}

const HourlyColors& ingosSunlight()
{
    static HourlyColors values = gipatSunlight();
    static const bool initialized = []()
    {
        values[2] = rgb(92,113,165);
        return true;
    }();
    Q_UNUSED(initialized)
    return values;
}

QVector3D hourlyColor(const HourlyColors& values, float hours)
{
    const int hour = int(qFloor(hours)) % 24;
    return lerp(values[size_t(hour)], values[size_t((hour + 1) % 24)],
                hours - float(hour));
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
                                               float sunStrength,
                                               bool caveEnvironment,
                                               const QString& region)
{
    SRenderEnvironment result;
    result.timeHours = timeHours;
    result.timeHours = std::fmod(std::fmod(result.timeHours, 24.0f) + 24.0f, 24.0f);
    Q_UNUSED(ambientStrength)
    Q_UNUSED(sunStrength)
    // game.exe parses WS_AMBIENT and WS_SUN_LIGHT but does not apply either
    // value. The normalized weather-table colors are the actual light terms.
    result.ambientStrength = 1.0f;
    result.sunStrength = 1.0f;

    result.skyRotationDegrees = result.timeHours * 15.0f;
    float elevationDegrees = 0.0f;
    if(result.timeHours >= 4.0f && result.timeHours <= 20.0f)
        elevationDegrees = 60.0f - 3.75f * qAbs(result.timeHours - 12.0f);
    else if(result.timeHours > 20.0f)
        elevationDegrees = 30.0f + (result.timeHours - 20.0f) * 11.25f;
    else
        elevationDegrees = 30.0f + (4.0f - result.timeHours) * 11.25f;
    const float elevation = qDegreesToRadians(elevationDegrees);
    // This is the direction travelled by the light ray, not the vector from
    // the receiver toward the sun. The native clock angle places noon at -X.
    const float azimuth = result.timeHours * (2.0f * float(M_PI) / 24.0f);
    const float horizontal = qCos(elevation);
    result.sunDirection = QVector3D(qCos(azimuth) * horizontal,
                                    qSin(azimuth) * horizontal,
                                    -qSin(elevation));

    if(caveEnvironment)
    {
        // texts.res/map.txt selects this path with "#sky cave". The stock
        // cave-light files for all three allods contain these same RGB values
        // at every hour, so only the MOB-authored WS_TIME selects the fixed
        // shadow direction; preview time must not alter cave illumination.
        result.sunColor = QVector3D(161.0f, 179.0f, 170.0f) / 255.0f;
        result.ambientColor = QVector3D(75.0f, 75.0f, 89.0f) / 255.0f;
        result.skyColor = QVector3D();
        result.fogColor = QVector3D(0.0f, 0.0f, 0.0f);
        result.fogStart = 180.0f;
        result.fogEnd = 620.0f;
        return result;
    }

    const bool ingos = region.compare(QStringLiteral("ingos"),
                                      Qt::CaseInsensitive) == 0;
    result.sunColor = hourlyColor(ingos ? ingosSunlight() : gipatSunlight(),
                                  result.timeHours);
    result.ambientColor = hourlyColor(ingos ? ingosAmbient() : gipatAmbient(),
                                      result.timeHours);
    result.skyColor = hourlyColor(ingos ? ingosSky() : gipatSky(),
                                  result.timeHours);
    result.fogColor = result.skyColor;
    // The stock sky cylinder is open below its horizon. Distant landscape must
    // reach the environment color before the finite editor map edge exposes
    // that opening.
    result.fogStart = 180.0f;
    result.fogEnd = 620.0f;
    return result;
}

SRenderEnvironment renderEnvironment(const CWorldSet* worldSet,
                                     bool overrideTime, float overrideHours,
                                     bool caveEnvironment,
                                     const QString& region)
{
    SRenderEnvironment defaults;
    return renderEnvironmentForValues(
        overrideTime && !caveEnvironment ? overrideHours
                     : worldFloat(worldSet, eWsTypeTime, defaults.timeHours),
        worldFloat(worldSet, eWsTypeAmbient, defaults.ambientStrength),
        worldFloat(worldSet, eWsTypeSunLight, defaults.sunStrength),
        caveEnvironment, region);
}

QVector3D shadowProjectionDirection(const QVector3D& sunDirection,
                                    const QVector3D& sunColor,
                                    float sunStrength,
                                    const QVector<SScenePointLight>& lights,
                                    const QVector3D& casterPosition,
                                    bool* pointLightInfluenced)
{
    if(pointLightInfluenced)
        *pointLightInfluenced = false;
    const auto normalizedColor = [](QVector3D color)
    {
        const float maximum = qMax(color.x(), qMax(color.y(), color.z()));
        if(maximum > 1.01f)
            color /= 255.0f;
        return color;
    };
    const auto luminance = [&](const QVector3D& color)
    {
        const QVector3D value = normalizedColor(color);
        return 0.3f * value.x() + 0.6f * value.y() + 0.1f * value.z();
    };

    QVector3D direction;
    if(!sunDirection.isNull())
        direction += sunDirection.normalized()
            * qMax(0.0f, sunStrength) * luminance(sunColor);
    for(const SScenePointLight& light : lights)
    {
        if(!light.castsShadow || light.range <= 0.0f)
            continue;
        const QVector3D offset = casterPosition - light.position;
        const float distanceSquared = offset.lengthSquared();
        const float rangeSquared = light.range * light.range;
        if(distanceSquared <= 0.000001f || distanceSquared >= rangeSquared)
            continue;
        const float attenuation = 1.0f - distanceSquared / rangeSquared;
        const float weight = attenuation * luminance(light.color);
        if(weight <= 0.0f)
            continue;
        direction += offset / qSqrt(distanceSquared) * weight;
        if(pointLightInfluenced)
            *pointLightInfluenced = true;
    }

    if(direction.lengthSquared() <= 0.000001f)
        direction = sunDirection.isNull()
            ? QVector3D(0.0f, 0.0f, -1.0f) : sunDirection.normalized();
    else
        direction.normalize();

    // Very shallow projections become unbounded and unstable. The native
    // projected-shadow path applies the same 20-degree floor after combining
    // eligible directional and point lights.
    const float horizontal = qSqrt(direction.x() * direction.x()
                                   + direction.y() * direction.y());
    if(horizontal > 0.000001f
            && qAtan2(qAbs(direction.z()), horizontal) < qDegreesToRadians(20.0f))
    {
        direction.setZ(-horizontal * qTan(qDegreesToRadians(20.0f)));
        direction.normalize();
    }
    return direction;
}

}
