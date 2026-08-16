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
    bool overlayUsesWorkingExtension,
    bool persistedWorkingDraft = false)
{
    return identityPublicationRequested || landMaterialFieldActive
            || overlayMaterialFieldActive || landUsesWorkingExtension
            || overlayUsesWorkingExtension || persistedWorkingDraft
        ? ETerrainMprSaveRoute::PublishModernAtlas
        : ETerrainMprSaveRoute::PlainMpr;
}

inline bool terrainMprUsesAdjacentModernAtlas(
    const QString& mprPath, const QString& terrainArchivePath)
{
    if(mprPath.trimmed().isEmpty() || terrainArchivePath.trimmed().isEmpty())
        return false;
    const QFileInfo mpr(mprPath);
    const QString expected = QDir::cleanPath(mpr.dir().filePath(
        mpr.completeBaseName() + QStringLiteral("-atlas.res")));
    const QString actual = QDir::cleanPath(
        QFileInfo(terrainArchivePath).absoluteFilePath());
    return QString::compare(expected, actual, Qt::CaseInsensitive) == 0;
}

inline bool terrainMprSaveActionEnabled(bool hasMpr, bool mprDirty,
                                        bool identityPublicationPending)
{
    return hasMpr && (mprDirty || identityPublicationPending);
}

inline bool terrainChangedSaveNeedsWorkingAtlasPackage(
    bool editableModernDraftRequested, bool workingAtlasAvailable,
    bool landUsesWorkingExtension, bool overlayUsesWorkingExtension)
{
    return (editableModernDraftRequested && workingAtlasAvailable)
        || landUsesWorkingExtension || overlayUsesWorkingExtension;
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

struct STerrainPublicationResourceChoicePlan
{
    QString initialPath;
    bool requiresExplicitChoice{true};
};

inline bool terrainPublicationPathInResourceRoots(
    const QString& resourcePath, const QStringList& configuredResourceRoots);

inline STerrainPublicationResourceChoicePlan terrainPublicationResourceChoicePlan(
    const QStringList& configuredResourceRoots, const QString& mprPath,
    const QString& activeArchivePath, bool saveOverCurrentMpr,
    bool persistedModernDraft)
{
    STerrainPublicationResourceChoicePlan plan;
    const QFileInfo activeArchive(activeArchivePath);
    const bool canReuseActiveArchive = saveOverCurrentMpr
        && !persistedModernDraft && activeArchive.isFile()
        && activeArchive.suffix().compare(
               QStringLiteral("res"), Qt::CaseInsensitive) == 0
        && terrainPublicationPathInResourceRoots(
               activeArchive.absoluteFilePath(), configuredResourceRoots);
    if(canReuseActiveArchive)
        plan.initialPath = activeArchive.absoluteFilePath();
    else
        plan.initialPath = terrainPublicationInitialResourcePath(
            configuredResourceRoots, mprPath);
    return plan;
}

inline QString terrainMapTestResourcePath(
    const QString& activeArchivePath, const QString& mprPath)
{
    const QFileInfo activeArchive(activeArchivePath);
    if(activeArchive.isFile()
       && activeArchive.suffix().compare(
              QStringLiteral("res"), Qt::CaseInsensitive) == 0)
        return activeArchive.absoluteFilePath();

    const QFileInfo mpr(mprPath);
    const QFileInfo adjacent(mpr.dir().filePath(
        mpr.completeBaseName() + QStringLiteral("-atlas.res")));
    if(adjacent.isFile())
        return adjacent.absoluteFilePath();
    const QFileInfo publishedTextures(
        mpr.dir().filePath(QStringLiteral("textures.res")));
    return publishedTextures.isFile()
        ? publishedTextures.absoluteFilePath() : QString();
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
