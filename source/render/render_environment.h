#ifndef RENDER_ENVIRONMENT_H
#define RENDER_ENVIRONMENT_H

#include <QVector3D>

class CWorldSet;

namespace ei {

struct SRenderEnvironment
{
    float timeHours{12.0f};
    float skyRotationDegrees{0.0f};
    float ambientStrength{0.2f};
    float sunStrength{0.6f};
    QVector3D sunDirection{0.0f, -0.7f, -0.7f};
    QVector3D sunColor{1.0f, 0.96f, 0.86f};
    QVector3D ambientColor{0.62f, 0.68f, 0.75f};
    QVector3D fogColor{0.58f, 0.66f, 0.72f};
    float fogStart{250.0f};
    float fogEnd{900.0f};
};

SRenderEnvironment renderEnvironment(const CWorldSet* worldSet,
                                     bool overrideTime, float overrideHours);
SRenderEnvironment renderEnvironmentForValues(float timeHours,
                                               float ambientStrength,
                                               float sunStrength);
float clockValueToHours(int hourMinuteValue);

}

#endif
