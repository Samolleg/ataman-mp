#ifndef TERRAIN_HD_PROVENANCE_H
#define TERRAIN_HD_PROVENANCE_H

#include "terrain_atlas_profile.h"
#include <QCryptographicHash>

// Audit convention: UTF-8 "<width>x<height>\n", then tightly packed decoded
// RGBA8888 scanlines. No encoded BC1 bytes, scaling or recompression.
inline QByteArray terrainProvenancePixelSha(const QImage& pixels)
{
    const auto rgba = pixels.convertToFormat(QImage::Format_RGBA8888);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(rgba.width()) + "x"
                 + QByteArray::number(rgba.height()) + "\n");
    for(int y=0;y<rgba.height();++y)
        hash.addData(reinterpret_cast<const char*>(rgba.constScanLine(y)),rgba.width()*4);
    return hash.result().toHex();
}

struct STerrainConfirmedHdProvenance
{
    STerrainConfirmedHdProvenance() = default;
    STerrainConfirmedHdProvenance(const QByteArray& sd, const QString& name, int type,
        ETerrainAtlasLayer targetLayer, int turn, const QString& donor, int slot,
        const QByteArray& hd, const QString& decision)
        : sdPixelSha256(sd), material(name), reviewedClass(type), layer(targetLayer),
          rotation(turn), hdNamespace(donor), hdSlot(slot), hdPixelSha256(hd), authority(decision) {}
    QByteArray sdPixelSha256;
    QString material;
    int reviewedClass{-1};
    ETerrainAtlasLayer layer{ETerrainAtlasLayer::Land};
    int rotation{0};
    QString hdNamespace;
    int hdSlot{-1};
    QByteArray hdPixelSha256;
    QString authority;
};

inline QVector<STerrainConfirmedHdProvenance> terrainConfirmedHdProvenance()
{
    // Owner decision 2026-10-11: "Stone подключай"; explicit provenance
    // approved after the SD comparison. This is NOT a fuzzy matching threshold.
    return {{"33982089439b4db72e7c34589c3899361031ee7dab5e9ae05b83784c031cd46e",
             "Stone / Desert",3,ETerrainAtlasLayer::Land,0,"bz16h",463,
             "d5855c1f92eca2b4e85d6f539fb616de44e2ce4a650dd7eaf25d24cc99737bcb",
             "owner-2026-10-11-Stone; ATAMAN_STONE_DESERT_HD_144_20261011"}};
}

struct STerrainStockHdOrigin
{
    QString sdNamespace;
    int sdSlot;
    QByteArray sdPixelSha256;
    QString hdNamespace;
    int hdSlot;
    QByteArray hdPixelSha256;
};

inline QVector<STerrainStockHdOrigin> terrainConfirmedStockHdOrigins()
{
    // Verified stock SD witness from the supplied Stone / Desert study.
    // Every target with these exact pixels may use the origin, irrespective
    // of its namespace/slot. Target semantics are never copied from the donor.
    return {{"zone18",463,
        "7466a4ce24c4539832414349def0b574149a932d2aa87f2884284f1bf966bcdd",
        "bz16h",463,"d5855c1f92eca2b4e85d6f539fb616de44e2ce4a650dd7eaf25d24cc99737bcb"}};
}

#endif
