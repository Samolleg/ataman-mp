#ifndef TERRAIN_SAVE_FLOW_POLICY_H
#define TERRAIN_SAVE_FLOW_POLICY_H

#include <QDir>
#include <QFileInfo>
#include <QStringList>

enum class ETerrainMprSaveRoute
{
    PlainMpr,
    SaveEditableModernDraft,
    PublishModernAtlas
};

enum class ETerrainMprSavePresentation
{
    PlainMpr,
    EditableModernDraft,
    PendingModernPublication
};

inline ETerrainMprSavePresentation terrainMprSavePresentationFor(
    ETerrainMprSaveRoute route)
{
    switch(route)
    {
    case ETerrainMprSaveRoute::SaveEditableModernDraft:
        return ETerrainMprSavePresentation::EditableModernDraft;
    case ETerrainMprSaveRoute::PublishModernAtlas:
        return ETerrainMprSavePresentation::PendingModernPublication;
    case ETerrainMprSaveRoute::PlainMpr:
    default:
        return ETerrainMprSavePresentation::PlainMpr;
    }
}

inline ETerrainMprSaveRoute terrainMprSaveRoute(
    bool identityPublicationRequested,
    bool landMaterialFieldActive,
    bool overlayMaterialFieldActive,
    bool landUsesWorkingExtension,
    bool overlayUsesWorkingExtension,
    bool persistedWorkingDraft = false,
    bool saveAs = false)
{
    // A persisted working draft is already a coherent MPR + adjacent RES pair.
    // Ordinary Save must keep updating that pair; publication is the explicit
    // Save As operation where the final MPR destination is selected first.
    if(persistedWorkingDraft && !saveAs && !identityPublicationRequested)
        return ETerrainMprSaveRoute::SaveEditableModernDraft;
    return identityPublicationRequested || landMaterialFieldActive
            || overlayMaterialFieldActive || landUsesWorkingExtension
            || overlayUsesWorkingExtension || persistedWorkingDraft
        ? ETerrainMprSaveRoute::PublishModernAtlas
        : ETerrainMprSaveRoute::PlainMpr;
}

inline bool terrainPublicationWouldOverwriteWorkingDraft(
    const QString& currentMprPath, const QString& publicationMprPath,
    bool persistedWorkingDraft)
{
    if(!persistedWorkingDraft || currentMprPath.trimmed().isEmpty()
       || publicationMprPath.trimmed().isEmpty())
        return false;
    const QString current = QDir::cleanPath(
        QFileInfo(currentMprPath).absoluteFilePath());
    const QString publication = QDir::cleanPath(
        QFileInfo(publicationMprPath).absoluteFilePath());
    return QString::compare(current, publication, Qt::CaseInsensitive) == 0;
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

inline bool terrainChangedSaveTransactionIsUsable(
    bool transactionBuilt, bool transactionHasErrors,
    bool transactionEmpty, bool editableModernDraftRequested,
    bool workingAtlasAvailable)
{
    if(!transactionBuilt || transactionHasErrors)
        return false;

    // Exporting an editable modern draft is also the supported way to turn a
    // finalized identity into a portable MPR + adjacent RES pair. In that
    // case the canonical MPR bytes may be unchanged; the newly staged atlas,
    // embedded profile, and semantic field are the actual export payload.
    return !transactionEmpty
        || (editableModernDraftRequested && workingAtlasAvailable);
}

inline bool terrainMapTestNeedsWorkingAtlasPackage(
    bool workingAtlasAvailable, bool landUsesWorkingExtension,
    bool overlayUsesWorkingExtension, int workingPageCount,
    int authoredPageCount)
{
    if(!workingAtlasAvailable)
        return false;
    // A generated/imported tile can reuse an empty physical slot in an
    // existing page.  Page growth therefore is only a compatibility fallback,
    // never the authority for deciding whether Test Current needs the working
    // atlas bytes.
    return landUsesWorkingExtension || overlayUsesWorkingExtension
        || workingPageCount > authoredPageCount;
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
