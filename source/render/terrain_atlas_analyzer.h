#ifndef TERRAIN_ATLAS_ANALYZER_H
#define TERRAIN_ATLAS_ANALYZER_H

#include <QImage>
#include <QVector>

#include "terrain_edit_model.h"

class CValidationReport;

struct STerrainAtlasMaterial
{
    qint32 id{0};
    qint32 surfaceType{0};
    int representativeSlot{-1};
    int pureVariantCount{0};
};

struct STerrainAtlasAnalysis
{
    CTerrainTransitionCatalog catalog;
    QVector<STerrainAtlasMaterial> materials;
    int analyzedSlots{0};
    int describedSlots{0};
    int ambiguousSlots{0};
    bool usable{false};
};

// Conservative, read-only analysis of already-authored atlas pixels. It finds
// visually uniform source tiles, groups natural variants, then describes other
// tiles by their four nearest source-material corners. Ambiguous slots are
// excluded rather than guessed.
class CTerrainAtlasAnalyzer
{
public:
    static STerrainAtlasAnalysis analyze(
        const QVector<QImage>& images, const QVector<qint32>& surfaceTypes,
        CValidationReport* report = nullptr);
};

#endif
