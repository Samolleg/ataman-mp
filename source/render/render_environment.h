#ifndef RENDER_ENVIRONMENT_H
#define RENDER_ENVIRONMENT_H

#include <QVector3D>
#include <QString>

class CWorldSet;

namespace ei {

constexpr int kMaxScenePointLights = 64;
constexpr int kMaxLocalShadowCasters = 16;

struct SScenePointLight
{
    QVector3D position;
    QVector3D color;
    float range{0.0f};
    bool castsShadow{false};
};

struct SRenderEnvironment
{
    float timeHours{12.0f};
    float skyRotationDegrees{0.0f};
    float ambientStrength{0.2f};
    float sunStrength{0.6f};
    QVector3D sunDirection{0.0f, -0.7f, -0.7f};
    QVector3D sunColor{1.0f, 0.96f, 0.86f};
    QVector3D ambientColor{0.62f, 0.68f, 0.75f};
    QVector3D skyColor{0.58f, 0.66f, 0.72f};
    QVector3D fogColor{0.58f, 0.66f, 0.72f};
    float fogStart{250.0f};
    float fogEnd{900.0f};
};

SRenderEnvironment renderEnvironment(const CWorldSet* worldSet,
                                     bool overrideTime, float overrideHours,
                                     bool caveEnvironment = false,
                                     const QString& region = QStringLiteral("gipat"));
SRenderEnvironment renderEnvironmentForValues(float timeHours,
                                               float ambientStrength,
                                               float sunStrength,
                                               bool caveEnvironment = false,
                                               const QString& region = QStringLiteral("gipat"));
float clockValueToHours(int hourMinuteValue);
QVector3D shadowProjectionDirection(const QVector3D& sunDirection,
                                    const QVector3D& sunColor,
                                    float sunStrength,
                                    const QVector<SScenePointLight>& lights,
                                    const QVector3D& casterPosition,
                                    bool* pointLightInfluenced = nullptr);

}

#endif
