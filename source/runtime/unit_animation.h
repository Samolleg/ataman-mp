#ifndef UNIT_ANIMATION_H
#define UNIT_ANIMATION_H

#include <QByteArray>
#include <QMap>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QStringList>
#include <QVector>
#include <QVector3D>

struct SAdbAction
{
    QString name;
    quint32 number{0};
    quint16 weaponMask{0};
    quint8 allowedState{0};
    quint8 actionType{0};
    quint8 modifier{0};
    quint8 stage{0};
    quint32 probability{0};
    quint32 length{0};
};

struct SAnimationChannel
{
    QString component;
    QVector<QQuaternion> rotations;
    QVector<QVector3D> translations;
    QVector<QVector<QVector3D>> morphations;
};

struct SAnimationClip
{
    QString actionName;
    int logicalLength{0};
    QMap<QString, SAnimationChannel> channels;
    QStringList diagnostics;
    bool valid() const { return logicalLength > 0 && !channels.isEmpty(); }
};

struct SAnimationLocalPose
{
    QMap<QString, QMatrix4x4> transforms;
    QMap<QString, QVector<QVector3D>> morphDeltas;
};

struct SAnimationSkeleton
{
    QMap<QString, QString> parents;
    QMap<QString, QVector3D> restOffsets;
};

struct SAnimationPerformanceCounters
{
    quint64 fullEvaluations{0};
    quint64 halfRateEvaluations{0};
    quint64 lowRateEvaluations{0};
    quint64 skippedOffscreen{0};
    quint64 staticFallbacks{0};
    quint64 poseEvaluations{0};
    quint64 poseCacheHits{0};
    quint64 componentsEvaluated{0};
};

enum class EAnimationDistanceTier { Skip, Full, HalfRate, LowRate };

QVector<SAdbAction> parseAdbActions(const QByteArray& bytes, QStringList* diagnostics = nullptr);
bool selectDefaultIdleAction(const QVector<SAdbAction>& actions, int weaponTypeId,
                             bool allowExtendedWeaponMask, SAdbAction* selected,
                             QString* diagnostic = nullptr);
bool selectJmvDefaultIdleAction(const QVector<SAdbAction>& actions, const QString& model,
                                int weaponTypeId, SAdbAction* selected,
                                QString* diagnostic = nullptr);
SAnimationClip parseNestedAnimationClip(const QByteArray& modelAnimationArchive,
                                        const QString& actionName, int logicalLength);
SAnimationLocalPose interpolateAnimationPose(const SAnimationClip& clip, double logicalFrame,
                                             const SAnimationSkeleton* skeleton = nullptr);
QMap<QString, QMatrix4x4> propagateAnimationHierarchy(
    const SAnimationLocalPose& pose, const SAnimationSkeleton& skeleton);
EAnimationDistanceTier animationDistanceTier(bool visible, bool selected, float distance);
double quantizedAnimationFrame(EAnimationDistanceTier tier, qint64 elapsedMilliseconds,
                               int logicalLength, quint32 phaseSeed);

class AnimationFrameContext
{
public:
    static AnimationFrameContext& instance();
    void beginFrame(const QVector3D& cameraPosition, qint64 elapsedMilliseconds, bool enabled,
                    bool cameraMoving = false);
    const QVector3D& cameraPosition() const { return m_cameraPosition; }
    qint64 elapsedMilliseconds() const { return m_elapsedMilliseconds; }
    bool enabled() const { return m_enabled; }
    bool claimColdVisualResolution();
    bool cameraMoving() const { return m_cameraMoving; }
    SAnimationPerformanceCounters& counters() { return m_counters; }
    const SAnimationPerformanceCounters& counters() const { return m_counters; }

private:
    QVector3D m_cameraPosition;
    qint64 m_elapsedMilliseconds{0};
    bool m_enabled{true};
    bool m_cameraMoving{false};
    int m_coldVisualResolutionsRemaining{1};
    SAnimationPerformanceCounters m_counters;
};

#endif
