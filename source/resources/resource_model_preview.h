#ifndef RESOURCE_MODEL_PREVIEW_H
#define RESOURCE_MODEL_PREVIEW_H

#include <QByteArray>
#include <QImage>
#include <QMap>
#include <QMatrix4x4>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector3D>
#include <QVector2D>

#include "runtime/unit_animation.h"

namespace ei {

struct SResourceModelPreview
{
    bool valid{false};
    QImage image;
    QString details;
    QStringList diagnostics;
    int componentCount{0};
    int triangleCount{0};
};

struct SResourceModelVertex
{
    QVector3D position;
    int sourceIndex{-1};
    QVector2D uv;
};

struct SResourceModelTriangle
{
    SResourceModelVertex vertices[3];
};

struct SResourceModelPart
{
    QString name;
    QVector<SResourceModelTriangle> triangles;
};

struct SResourceModelScene
{
    bool valid{false};
    QString kind;
    QVector<SResourceModelPart> parts;
    SAnimationSkeleton skeleton;
    QStringList diagnostics;
    int triangleCount{0};
};

struct SResourceModelView
{
    float modelYawDegrees{0.0f};
    float modelPitchDegrees{0.0f};
    float azimuthDegrees{0.0f};
    float elevationDegrees{0.0f};
    float zoom{1.0f};
    QVector3D pan;
    QSet<QString> visibleParts;
    bool filterParts{false};
    QMap<QString, QMatrix4x4> poses;
    QMap<QString, QVector<QVector3D>> morphDeltas;
};

float resourceModelDefaultElevationDegrees();
float resourceModelDefaultAzimuthDegrees();
bool isHumanoidAssemblyStem(const QString& modelStem);
QSet<QString> defaultVisibleModelParts(const SResourceModelScene& scene,
                                       const QString& modelStem);
int inferredAnimationLogicalLength(const SAnimationClip& clip);
int resourceModelPreviewTriangleBudget(const QString& partName, int authoredTriangles,
                                       int partCount, int anatomyTriangles);
bool resourcePayloadLooksText(const QByteArray& bytes);
SResourceModelScene buildFig8Scene(const QByteArray& bytes);
SResourceModelScene buildModScene(const QByteArray& bytes,
                                  const QByteArray& companionBon = QByteArray());
QImage renderResourceModelScene(const SResourceModelScene& scene,
                                const SResourceModelView& view,
                                const QSize& size = QSize(640, 480));

// Bounded, CPU-only preview for browser workers. It follows the established
// FIG8 vertex-component order and MOD/LNK hierarchy but creates no scene node,
// OpenGL object, cache entry, or mutable resource state.
SResourceModelPreview previewFig8(const QByteArray& bytes, const QSize& size = QSize(640, 480));
SResourceModelPreview previewModAssembly(const QByteArray& bytes,
                                         const QByteArray& companionBon = QByteArray(),
                                         const QSize& size = QSize(640, 480));

// ANM is a nested RES whose members are actions and then component channels.
// Without provider ADB metadata, action timing/selection is intentionally not
// inferred; this inventory is paired with a same-stem model preview by the UI.
QStringList inspectAnimationArchive(const QByteArray& bytes, int* actionCount = nullptr,
                                    int* channelCount = nullptr);
QStringList inspectBoneArchive(const QByteArray& bytes, int* componentCount = nullptr);

}

#endif // RESOURCE_MODEL_PREVIEW_H
