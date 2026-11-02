// Test-only calibration of the pre-fix renderer at neutral noon. The production
// draw/shader objects remain those of the separately rebuilt baseline archive.
// This is not an owner-settings or original-colored-noon reproduction.
#define renderEnvironment authoredReferenceEnvironment
#include "../source/render/render_environment.cpp"
#undef renderEnvironment

namespace ei {
SRenderEnvironment renderEnvironment(const CWorldSet* worldSet,
                                     bool overrideTime, float overrideHours,
                                     bool caveEnvironment, const QString& region)
{
    auto result=authoredReferenceEnvironment(worldSet,overrideTime,overrideHours,caveEnvironment,region);
    const auto noon=renderEnvironmentForValues(12.0f,1.0f,1.0f);
    const float ambient=QVector3D::dotProduct(noon.ambientColor,QVector3D(.2126f,.7152f,.0722f));
    result.sunColor=QVector3D(1,1,1);
    result.ambientColor=QVector3D(ambient,ambient,ambient);
    return result;
}
}
