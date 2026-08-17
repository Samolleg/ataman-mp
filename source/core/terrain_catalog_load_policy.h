#ifndef TERRAIN_CATALOG_LOAD_POLICY_H
#define TERRAIN_CATALOG_LOAD_POLICY_H

enum class ETerrainCatalogLoadAction
{
    None,
    QueueAfterVisibleFrame,
    BuildSynchronously
};

// Terrain pixels and their exact RES binding are part of opening an MPR.
// Material catalogs, semantic paint fields, and palette thumbnails are paint
// tooling.  Keep the latter behind the first presented landscape frame while
// still allowing an explicit paint/save action to demand them immediately.
inline ETerrainCatalogLoadAction terrainCatalogLoadAction(
    bool pending, bool queued, bool building,
    bool visibleFrameCompleted, bool interactionRequested)
{
    if(!pending || building)
        return ETerrainCatalogLoadAction::None;
    if(interactionRequested)
        return ETerrainCatalogLoadAction::BuildSynchronously;
    if(visibleFrameCompleted && !queued)
        return ETerrainCatalogLoadAction::QueueAfterVisibleFrame;
    return ETerrainCatalogLoadAction::None;
}

#endif
