#ifndef TERRAIN_ATLAS_ANALYZER_H
#define TERRAIN_ATLAS_ANALYZER_H

#include <QString>
#include <QVector>

#include "terrain_edit_model.h"

class CValidationReport;

enum class ETerrainAtlasLayer
{
    Land,
    Overlay
};

struct STerrainAtlasMaterial
{
    qint32 id{0};
    QString name;
    ETerrainAtlasLayer layer{ETerrainAtlasLayer::Land};
    bool underwater{false};
    int representativeSlot{-1};
    int pureVariantCount{0};
};

struct STerrainAtlasAnalysis
{
    CTerrainTransitionCatalog landCatalog;
    CTerrainTransitionCatalog overlayCatalog;
    QVector<STerrainAtlasMaterial> materials;
    QString templateName;
    int expectedSlots{0};
    int describedSlots{0};
    bool usable{false};

    const CTerrainTransitionCatalog& catalog(ETerrainTileLayer layer) const
    {
        return layer == ETerrainTileLayer::Land ? landCatalog : overlayCatalog;
    }
};

// Resolves an MPR namespace to an authored slot-semantic contract. Atlas pixels
// remain user supplied and are never edited; the contract only describes which
// material family occupies each authored slot corner. Unknown layouts fail
// closed instead of being guessed from colors or gameplay surface categories.
class CTerrainAtlasAnalyzer
{
public:
    static STerrainAtlasAnalysis analyze(const QString& mprNamespace,
                                         int atlasSlotCount,
                                         CValidationReport* report = nullptr);
};

#endif
