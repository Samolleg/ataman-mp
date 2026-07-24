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
    // Overlay records use the same packed atlas reference as land records.
    // Some shipped maps intentionally place land-family slots on an active
    // overlay surface, so the opt-in overlay compatibility mode needs a
    // catalog that can decode either authored family without inventing slots.
    CTerrainTransitionCatalog overlayCompatibleCatalog;
    QVector<STerrainAtlasMaterial> materials;
    QString templateName;
    int expectedSlots{0};
    int describedSlots{0};
    bool usable{false};

    const CTerrainTransitionCatalog& catalog(ETerrainTileLayer layer) const
    {
        return layer == ETerrainTileLayer::Land ? landCatalog : overlayCatalog;
    }

    const CTerrainTransitionCatalog& overlayCatalogWithLandFamilies() const
    {
        return overlayCompatibleCatalog;
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
