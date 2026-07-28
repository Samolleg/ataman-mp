#ifndef TERRAIN_VIRTUAL_ATLAS_H
#define TERRAIN_VIRTUAL_ATLAS_H

#include <QByteArray>
#include <QImage>
#include <QMap>
#include <QPoint>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include "terrain_edit_model.h"

class CValidationReport;
class CMprDocument;

struct STerrainVirtualTile
{
    STerrainTransitionSignature signature;
    bool authored{false};
    quint16 authoredPacked{0};
    int generatedIndex{-1};
    quint8 generatedRotation{0};
};

struct STerrainAtlasUsage
{
    QMap<int, quint64> authoredSlots;
    QMap<int, quint64> generatedTiles;
    quint64 cellCount{0};
};

struct STerrainFinalizedTile
{
    STerrainTransitionSignature canonicalSignature;
    QImage pixels;
    QByteArray pixelSha256;
    bool authored{false};
    int sourceSlot{-1};
};

struct STerrainAtlasFinalization
{
    bool success{false};
    QString error;
    QString isolatedNamespace;
    int capacityLimit{512};
    int requiredSlots{0};
    int textureCount{0};
    QVector<quint16> packedCells;
    QVector<STerrainFinalizedTile> tiles;
    QVector<QImage> pages;
    QVector<QByteArray> pageMmpPayloads;
    QVector<qint32> tileTypes;
    QMap<int, int> authoredSlotRemap;
    STerrainAtlasUsage usage;
    QByteArray stableSha256;
};

struct STerrainPortableAtlasPackage
{
    bool success{false};
    QString error;
    QByteArray mprBytes;
    QByteArray resourceArchiveBytes;
    QStringList resourceMembers;
};

// Canonical desired material ownership lives on terrain vertices and is not
// limited by authored atlas slots. Physical atlas references are derived only
// for preview/finalization.
class CTerrainVirtualMaterialField
{
public:
    bool resize(int tileWidth, int tileHeight, qint32 initialMaterial = 0);
    int tileWidth() const { return m_tileWidth; }
    int tileHeight() const { return m_tileHeight; }
    int vertexWidth() const { return m_tileWidth + 1; }
    int vertexHeight() const { return m_tileHeight + 1; }
    qint32 material(int vertexX, int vertexY, bool* found = nullptr) const;
    bool setMaterial(int vertexX, int vertexY, qint32 material);
    int paint(const STerrainTileBrush& brush, qint32 material);
    STerrainTransitionSignature cellSignature(int cellX, int cellY,
                                               bool* found = nullptr) const;
    QByteArray stableSha256() const;

private:
    int m_tileWidth{0};
    int m_tileHeight{0};
    QVector<qint32> m_materials;
};

// Resolves a virtual field against authored tiles first, then deterministically
// synthesizes exact missing tuples. Generated entries are canonicalized across
// quarter-turns and deduplicated again by pixels.
class CTerrainVirtualAtlas
{
public:
    CTerrainVirtualAtlas(const CTerrainTransitionCatalog& catalog,
                         const QVector<QImage>& authoredTiles,
                         quint32 distributionSeed = 0,
                         const QMap<qint32, QImage>& externalMaterialSources = {});

    bool isValid() const { return m_valid; }
    QString error() const { return m_error; }
    STerrainVirtualTile resolve(const STerrainTransitionSignature& signature,
                                int cellX, int cellY,
                                CValidationReport* report = nullptr);
    const QVector<STerrainFinalizedTile>& generatedTiles() const
        { return m_generatedTiles; }

    STerrainAtlasFinalization finalize(const CTerrainVirtualMaterialField& field,
                                       const QString& sourceNamespace,
                                       int capacityLimit = 512,
                                       const QVector<qint32>& sourceTileTypes = {},
                                       CValidationReport* report = nullptr,
                                       const QVector<quint16>& pinnedPackedTiles = {});

    static QImage composePage(const QVector<QImage>& tiles, int firstTile,
                              int tileSize);
    static STerrainPortableAtlasPackage buildPortablePackage(
        const CMprDocument& source, const STerrainAtlasFinalization& finalization,
        ETerrainTileLayer layer, CValidationReport* report = nullptr);

private:
    QByteArray signatureKey(const STerrainTransitionSignature& signature) const;
    STerrainTransitionSignature canonicalSignature(
        const STerrainTransitionSignature& signature, quint8* rotation) const;
    QImage generateTile(const STerrainTransitionSignature& gridSignature,
                        CValidationReport* report) const;

    const CTerrainTransitionCatalog& m_catalog;
    QVector<QImage> m_authoredTiles;
    QMap<qint32, QImage> m_materialSources;
    QMap<qint32, int> m_materialSourceSlots;
    QMap<QByteArray, int> m_generatedBySignature;
    QMap<QByteArray, int> m_generatedByPixels;
    QVector<STerrainFinalizedTile> m_generatedTiles;
    quint32 m_distributionSeed{0};
    bool m_valid{false};
    QString m_error;
};

// A bounded physical cache used only for the live editor preview. It realizes
// authored or generated exact tuples into currently unused slots, while the
// unbounded vertex-material field remains authoritative. Finalization does not
// trust this cache; it rebuilds a clean isolated atlas from the field.
class CTerrainWorkingAtlas
{
public:
    CTerrainWorkingAtlas(const CTerrainTransitionCatalog& authoredCatalog,
                         const QVector<QImage>& authoredTiles,
                         quint32 distributionSeed = 0,
                         int capacityLimit = 512,
                         const QMap<qint32, QImage>& externalMaterialSources = {});

    bool isValid() const;
    QString error() const;
    const CTerrainTransitionCatalog& catalog() const { return m_workingCatalog; }
    const QVector<QImage>& tileImages() const { return m_workingImages; }
    int generatedTileCount() const;
    QSet<int> takeChangedSlots();

    quint16 resolve(const STerrainTransitionSignature& signature,
                    int cellX, int cellY,
                    QMap<int, quint64>* workingUsage,
                    const QSet<int>& protectedSlots,
                    bool* generated = nullptr,
                    CValidationReport* report = nullptr);

private:
    int realize(bool generated, int sourceIndex,
                QMap<int, quint64>* workingUsage,
                const QSet<int>& protectedSlots,
                CValidationReport* report);
    void installCatalogVariants(bool generated, int sourceIndex,
                                int workingSlot);

    CTerrainTransitionCatalog m_authoredCatalog;
    CTerrainTransitionCatalog m_workingCatalog;
    QVector<QImage> m_authoredImages;
    QVector<QImage> m_workingImages;
    QSharedPointer<CTerrainVirtualAtlas> m_resolver;
    QMap<QPair<int, int>, int> m_virtualToWorking;
    QMap<int, QPair<int, int>> m_workingToVirtual;
    QSet<int> m_changedSlots;
    int m_capacityLimit{0};
};

#endif // TERRAIN_VIRTUAL_ATLAS_H
