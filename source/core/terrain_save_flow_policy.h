#ifndef TERRAIN_SAVE_FLOW_POLICY_H
#define TERRAIN_SAVE_FLOW_POLICY_H

#include <QDir>
#include <QFileInfo>
#include <QStringList>

enum class ETerrainMprSaveRoute
{
    PlainMpr,
    PublishModernAtlas
};

inline ETerrainMprSaveRoute terrainMprSaveRoute(
    bool identityPublicationRequested,
    bool landMaterialFieldActive,
    bool overlayMaterialFieldActive,
    bool landUsesWorkingExtension,
    bool overlayUsesWorkingExtension)
{
    return identityPublicationRequested || landMaterialFieldActive
            || overlayMaterialFieldActive || landUsesWorkingExtension
            || overlayUsesWorkingExtension
        ? ETerrainMprSaveRoute::PublishModernAtlas
        : ETerrainMprSaveRoute::PlainMpr;
}

inline bool terrainMprSaveActionEnabled(bool hasMpr, bool mprDirty,
                                        bool identityPublicationPending)
{
    return hasMpr && (mprDirty || identityPublicationPending);
}

enum class ETerrainPublicationPrimaryLayer
{
    None,
    Land,
    Overlay
};

struct STerrainPublicationLayerPlan
{
    bool valid{false};
    ETerrainPublicationPrimaryLayer primary{
        ETerrainPublicationPrimaryLayer::None};
    bool publishLand{false};
    bool publishOverlay{false};
};

inline STerrainPublicationLayerPlan terrainPublicationLayerPlan(
    bool landActive, bool overlayActive)
{
    STerrainPublicationLayerPlan plan;
    plan.publishLand = landActive;
    plan.publishOverlay = overlayActive;
    plan.valid = landActive || overlayActive;
    if(landActive)
        plan.primary = ETerrainPublicationPrimaryLayer::Land;
    else if(overlayActive)
        plan.primary = ETerrainPublicationPrimaryLayer::Overlay;
    return plan;
}

inline QString terrainPublicationInitialResourcePath(
    const QStringList& configuredResourceRoots, const QString& mprPath)
{
    for(const QString& configuredRoot : configuredResourceRoots)
    {
        const QFileInfo root(configuredRoot);
        if(root.isDir())
            return QDir(root.absoluteFilePath()).filePath(
                QStringLiteral("textures.res"));
    }
    const QFileInfo mpr(mprPath);
    const QString fallback = mpr.absolutePath().isEmpty()
        ? QDir::homePath() : mpr.absolutePath();
    return QDir(fallback).filePath(QStringLiteral("textures.res"));
}

inline bool terrainPublicationPathInResourceRoots(
    const QString& resourcePath, const QStringList& configuredResourceRoots)
{
    const QString candidate = QDir::fromNativeSeparators(QDir::cleanPath(
        QFileInfo(resourcePath).absoluteFilePath()));
    for(const QString& configuredRoot : configuredResourceRoots)
    {
        const QString root = QDir::fromNativeSeparators(QDir::cleanPath(
            QFileInfo(configuredRoot).absoluteFilePath()));
        if(candidate.compare(root, Qt::CaseInsensitive) == 0
           || candidate.startsWith(root + QLatin1Char('/'),
                                   Qt::CaseInsensitive))
            return true;
    }
    return false;
}

#endif
