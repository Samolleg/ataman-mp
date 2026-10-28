#ifndef TOPDOWN_CAPTURE_H
#define TOPDOWN_CAPTURE_H
#include "render_queue.h"
#include <QImage>

// Internal render passes, never user/configuration switches. All use the
// existing terrain shader, sector buffers, object meshes and render queue.
enum class ETopdownPass { None, Composite, Terrain, Relief, Water, WaterMask,
                         Structures, StructuresMask, Vegetation };
inline bool topdownTransparent(ETopdownPass p) {
    return p==ETopdownPass::Water || p==ETopdownPass::WaterMask
        || p==ETopdownPass::Structures || p==ETopdownPass::StructuresMask
        || p==ETopdownPass::Vegetation;
}
inline bool topdownMask(ETopdownPass p) {
    return p==ETopdownPass::WaterMask || p==ETopdownPass::StructuresMask;
}
#endif
