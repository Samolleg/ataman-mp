#include "terrain_atlas_analyzer.h"

#include <QByteArray>
#include <QSet>

#include "endurance_document.h"
#include "mpr_document.h"

namespace {
struct SMaterialDefinition
{
    qint32 id;
    const char* name;
    ETerrainAtlasLayer layer;
};

// Slot topology for the stock Zone1 namespace. This is compatibility metadata,
// not atlas imagery. The four bytes per slot are NW, NE, SE, SW material ids.
// Research attribution is recorded in NOTICE.md.
const char kZone1Signatures[] =
    "AQEBAQICAgIDAwMDBAQEBAQEBAQEBAQEBAQEBAQEBAQFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQYGBgYGBgYGBgYG"
    "BgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYG"
    "BgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGBwcHBwcHBwcDAwQEAwMEBAMDBAQDAwQEAwMEBAMDBAQD"
    "AwQEAwMEBAMDBAQDAwQEAwMEBAMDBAQDAwQEAwMEBAMEBAQDBAQEAwQEBAMEBAQDBAQEAwQEBAMEBAQDBAQEAwQEBAMEBAQDBAQE"
    "AwQEBAMEBAQDBAQEAwQEBAgICAgHBAQEBwQEBAcEBAQHBAQEBwQEBAcEBAQHBAQEBwQEBAcEBAQHBAQEBwQEBAcEBAQHBAQEBwQE"
    "BAMDAwQDAwMEAwMDBAMDAwQDAwMEAwMDBAMDAwQDAwMEAwMDBAMDAwQDAwMEAwMDBAMDAwQDAwMEBwcEBAcHBAQHBwQEBwcEBAcH"
    "BAQHBwQEBwcEBAcHBAQHBwQEBwcEBAcHBAQHBwQEBwcEBAQEBAIEBAQCBAQEAgQEBAIEBAQCBAQEAgQEBAIHBwcEBwcHBAcHBwQH"
    "BwcEBwcHBAcHBwQHBwcEBwcHBAcHBwQHBwcEBwcHBAcHBwQBAQEIAQEBCAEBAQgBAQEIAQEBCAEBAQgBAQEIAQEBCAEBAQgBAQEI"
    "AQEBCAEBCAgBAQgIAQEICAEBCAgBAQgIAQEICAEBCAgBAQgIAQEICAEBCAgHBAcEBwQHBAcEBwQHBAcEBwQHBAcEBwQHBAcEBwQH"
    "BAcEBwQJBAQECQQEBAkEBAQJBAQECQQEBAkEBAQJBAQECQQEBAkEBAQEBAICBAQCAgQEAgIEBAICBAQCAgQEAgIEBAICBAQCAgQE"
    "AgIICAQEAwMDAgkECQIJAwkECAgCAggIAgIICAICCAgCAggIAgIICAICCAgCAggIAgIDBAMEAwQDBAMEAwQDBAMEAwQDBAMEAwQD"
    "BAMEAwQDBAMEAwQIAgICCAICAggCAgIIAgICCAICAggCAgIIAgICAQgICAEICAgBCAgIAQgICAEICAgBCAgIAQgICAEICAgJCQQE"
    "CQkEBAkJBAQJCQQECQkEBAkJBAQJCQQECQkEBAgICAIICAgCCAgIAggICAIICAgCCAgIAggICAIJBAQECQQEBAkEBAQJBAQECQQE"
    "BAkEBAQJBAQEBAICAgQCAgIEAgICBAICAgQCAgIEAgICBAICAgkJAgIDAgQECQkDBAUFBgYFBQYGBQUGBgUFBgYFBQYGBQUGBgcH"
    "BwgHBwcIBwcHCAcHBwgHBwcIBwcHCAcHBwgIBAQECAQEBAgEBAQIBAQECAQEBAgEBAQHAgICBwICAgcCAgIHAgICBwICAgcCAgIB"
    "AwMDAQMDAwEDAwMBAwMDAQMDAwEDAwMHBwgIBwcICAcHCAgHBwgIBwcICAcHCAgFBgYGBQYGBgUGBgYFBgYGBQYGBgUGBgYJAwMD"
    "CQMDAwkDAwMJAwMDCQMDAwkDAwMHBwICBwcCAgcHAgIHBwICBwcCAgUFBQYFBQUGBQUFBgUFBQYFBQUGBQUFBgkECQQDAgICAwQC"
    "AgcJBwQHBwcCBwcHAgcHBwIHBwcCBwcHAgcHBwIBAQcHAQEHBwEBBwcBAQcHAQEHBwEBBwcJAgICCQICAgkCAgIJAgICCQICAgkE"
    "BAMKAwQEBwMEBAcDBAQHAwQEBwMEBAkECQkJCQkJBwMDBAcEAwQHBAMEBwQDBAcEAwQHBAMEAQgBCAEIAQgBCAEIAQgBCAEIAQgH"
    "BAkJBwcECQcHBwMJBAMEBwMEAwEHAQgJCQkECQkJBAkJCQQJCQkECQkJBAcEBAMHBAQDBwQEAwcEBAMJBAMDCQQDAwkEAwMJBAMD"
    "AQgHBwEIBwcLCwcHAQgHBwkJCQIJCQkCCQkJAgMEBAIDBAQCAQEHCAEBBwgBAQcIBwQDAwcEAwMHBAMDBwgICAcICAgHCAgIAQEI"
    "BwEBCAcBAQgHCQMDBAkDAwQJAwMECQMEBAkDBAQJAwQECQQJBAkECQQJBAkECQMDBAkDAwQJAwMEBwcDBAcHAwQHBwMDBwcDAwEI"
    "AggBCAIIBwcEAwcHBAMBBwgIAQcICAMDAgIDAwICCQkDAwkJAwMJBAMECQQDBAkEAwQJBAMECQQEAwkEBAMJBAQDCQQEAwEHBwcB"
    "BwcHAQEBBwEBAQcJCQkECQkJBAkJCQQJCQkECQkEBAkJBAQHCQQEDAkEBAQCBAIEAgQCCQkEBAkJBAQDAwQCCQIJCQkEBAkJBAQJ"
    "CQQECQkEAwMJBAMDCQQDAwMCAgQDAgIEAwICBAEIBwgBCAcIAQgHCAkDAwMJAwMDCQMDAwMEAgQDBAIEAwQCBAEHBwgBBwcIAQcH"
    "CAkJCQMJCQkJCQQEAgkECQIJAgQECQMEAwkDBAkJAgICCQQJAwkDAwkJAgIJCQIEBAkEBwcJCQMDCQMEBAkJCQIJAwkECQQCBAkJ"
    "CQQJBwQJCQkCAgcHCQkJCQMDCQkJCQECAggHBwgEAQgCAgcHCAIHBwQICQkECQcHAgQHCQkJBwkJBAkJAwkHCAcIBwgCCAcHBwkH"
    "AwcEBwQHAgMCBAIJBAICBwQEAgcCAgQ=";

const SMaterialDefinition kZone1Materials[] = {
    {1, "Ground / dark water", ETerrainAtlasLayer::Land},
    {2, "Ground / common", ETerrainAtlasLayer::Land},
    {3, "Grass / dark", ETerrainAtlasLayer::Land},
    {4, "Grass / common", ETerrainAtlasLayer::Land},
    {5, "Swamp", ETerrainAtlasLayer::Overlay},
    {6, "Water / still", ETerrainAtlasLayer::Overlay},
    {7, "Stone / lime", ETerrainAtlasLayer::Land},
    {8, "Ground / wet", ETerrainAtlasLayer::Land},
    {9, "Grass / irregular", ETerrainAtlasLayer::Land},
    {10, "Stone / greenish", ETerrainAtlasLayer::Land},
    {11, "Rock / watered", ETerrainAtlasLayer::Land},
    {12, "Grass / winter normal", ETerrainAtlasLayer::Land}
};

ETerrainAtlasLayer materialLayer(qint32 id)
{
    for(const SMaterialDefinition& material : kZone1Materials)
        if(material.id == id)
            return material.layer;
    return ETerrainAtlasLayer::Land;
}

bool isUniform(const STerrainTransitionSignature& signature)
{
    return signature.northWest == signature.northEast
        && signature.northWest == signature.southEast
        && signature.northWest == signature.southWest;
}
}

STerrainAtlasAnalysis CTerrainAtlasAnalyzer::analyze(
    const QString& mprNamespace, int atlasSlotCount, CValidationReport* report)
{
    STerrainAtlasAnalysis result;
    if(mprNamespace.compare(QStringLiteral("zone1"), Qt::CaseInsensitive) != 0)
    {
        if(report)
            report->add("terrain.atlas_template", EValidationSeverity::Error,
                        "No authored terrain slot template is registered for this MPR namespace",
                        mprNamespace);
        return result;
    }

    result.templateName = QStringLiteral("Zone1 stock slot layout");
    result.expectedSlots = 512;
    if(atlasSlotCount != result.expectedSlots)
    {
        if(report)
            report->add("terrain.atlas_slot_count", EValidationSeverity::Error,
                        QString("The registered Zone1 template requires 512 slots, but the loaded atlas has %1")
                            .arg(atlasSlotCount));
        return result;
    }

    const QByteArray signatures = QByteArray::fromBase64(kZone1Signatures);
    if(signatures.size() != result.expectedSlots * 4)
    {
        if(report)
            report->add("terrain.atlas_template_data", EValidationSeverity::Error,
                        "The internal Zone1 terrain template failed its size check");
        return result;
    }

    QSet<qint32> pureSeen;
    QMap<qint32, int> pureCounts;
    QMap<qint32, int> representatives;
    for(int slot = 0; slot < result.expectedSlots; ++slot)
    {
        STerrainTransitionSignature signature;
        signature.northWest = quint8(signatures.at(slot * 4));
        signature.northEast = quint8(signatures.at(slot * 4 + 1));
        signature.southEast = quint8(signatures.at(slot * 4 + 2));
        signature.southWest = quint8(signatures.at(slot * 4 + 3));
        const ETerrainAtlasLayer layer = materialLayer(signature.northWest);
        const bool sameLayer = materialLayer(signature.northEast) == layer
            && materialLayer(signature.southEast) == layer
            && materialLayer(signature.southWest) == layer;
        if(!sameLayer)
        {
            if(report)
                report->add("terrain.atlas_layer_mix", EValidationSeverity::Error,
                            "The internal Zone1 template mixes land and overlay materials",
                            QString::number(slot));
            return STerrainAtlasAnalysis();
        }

        CTerrainTransitionCatalog* catalog = layer == ETerrainAtlasLayer::Land
            ? &result.landCatalog : &result.overlayCatalog;
        for(int rotation = 0; rotation < 4; ++rotation)
        {
            SMprPackedTile packed;
            packed.atlasIndex = quint8(slot / 64);
            packed.tileIndex = quint8(slot % 64);
            packed.rotation = quint8(rotation);
            STerrainTransitionVariant variant;
            variant.signature = CTerrainTransitionCatalog::rotated(signature, rotation);
            variant.packedTile = packed.encode();
            if(!catalog->addVariant(variant, report))
                return STerrainAtlasAnalysis();
        }
        if(isUniform(signature))
        {
            pureSeen.insert(signature.northWest);
            pureCounts[signature.northWest] += 1;
            if(!representatives.contains(signature.northWest))
                representatives.insert(signature.northWest, slot);
        }
        ++result.describedSlots;
    }

    for(const SMaterialDefinition& definition : kZone1Materials)
    {
        if(!pureSeen.contains(definition.id))
            continue; // Transition-only ingredients are decoded but are not paint choices.
        STerrainAtlasMaterial material;
        material.id = definition.id;
        material.name = QString::fromUtf8(definition.name);
        material.layer = definition.layer;
        material.representativeSlot = representatives.value(definition.id, -1);
        material.pureVariantCount = pureCounts.value(definition.id);
        result.materials.append(material);
    }
    result.usable = result.landCatalog.size() > 0 && result.overlayCatalog.size() > 0;
    return result;
}
