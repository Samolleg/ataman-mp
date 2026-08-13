#ifndef TERRAIN_SAVE_FLOW_POLICY_H
#define TERRAIN_SAVE_FLOW_POLICY_H

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

inline bool terrainPublishNeedsMprDestinationDialog(
    bool saveOrSaveAsAlreadySelectedDestination)
{
    return !saveOrSaveAsAlreadySelectedDestination;
}

#endif
