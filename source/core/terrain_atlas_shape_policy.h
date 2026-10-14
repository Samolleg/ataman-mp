#ifndef TERRAIN_ATLAS_SHAPE_POLICY_H
#define TERRAIN_ATLAS_SHAPE_POLICY_H

inline bool terrainAtlasLogicalCountsFitPhysicalPages(
    int textureCount, int logicalTileCount, int physicalTileCount,
    int tileTypeCount)
{
    if(textureCount <= 0 || logicalTileCount <= 0
       || physicalTileCount != textureCount * 64
       || tileTypeCount != logicalTileCount)
        return false;
    // MMP pages are physical 8x8 containers.  The final page may contain
    // padding images that have no logical MP tile type; it must still be the
    // minimal page count for the logical atlas.
    return (logicalTileCount + 63) / 64 == textureCount;
}

#endif // TERRAIN_ATLAS_SHAPE_POLICY_H
