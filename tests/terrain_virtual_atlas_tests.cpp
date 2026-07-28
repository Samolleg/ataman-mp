#include <QtTest>
#include <QtEndian>
#include <QDir>
#include <QFile>

#include "endurance_document.h"
#include "mmp_codec.h"
#include "mmp_inspector.h"
#include "mpr_document.h"
#include "res_archive_document.h"
#include "terrain_virtual_atlas.h"

namespace {
STerrainTransitionSignature signature(qint32 nw,qint32 ne,qint32 se,qint32 sw)
{
    STerrainTransitionSignature value;
    value.northWest=nw; value.northEast=ne;
    value.southEast=se; value.southWest=sw;
    return value;
}

QImage solid(QRgb color)
{
    QImage image(8,8,QImage::Format_RGBA8888);
    image.fill(QColor::fromRgba(color));
    return image;
}

struct Fixture
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;

    Fixture(bool duplicatePixels=false)
    {
        images << solid(qRgba(220,20,20,255))
               << solid(qRgba(20,220,20,255))
               << solid(qRgba(20,20,220,255))
               << solid(duplicatePixels?qRgba(20,20,220,255):qRgba(230,220,20,255));
        for(int material=1;material<=4;++material)
        {
            STerrainTransitionVariant variant;
            variant.signature=signature(material,material,material,material);
            SMprPackedTile packed;
            packed.tileIndex=quint8(material-1);
            variant.packedTile=packed.encode();
            catalog.addVariant(variant);
        }
    }
};

QByteArray portableMpr()
{
    QByteArray mp;
    QDataStream stream(&mp,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream<<quint32(0xCE4AF672)<<float(100.0f)<<quint32(1)<<quint32(1)
          <<quint32(4)<<quint32(64)<<quint32(4)<<quint32(8)
          <<quint16(0)<<quint32(0);
    stream<<qint32(1)<<qint32(2)<<qint32(3)<<qint32(4);
    QByteArray sector;
    QDataStream sec(&sector,QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec<<quint32(0xCF4BF774)<<quint8(0);
    sector.append(QByteArray(33*33*8,'\0'));
    sector.append(QByteArray(16*16*2,'\0'));
    QByteArray empty(16,'\0');
    qToLittleEndian<quint32>(0x019CE23C,reinterpret_cast<uchar*>(empty.data()));
    qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
    CResArchiveDocument archive;QStringList errors;
    if(!archive.loadBytes(empty,"empty",&errors))return {};
    QByteArray first;
    if(!archive.buildWithAppendedMember("CaveSet.MP",mp,0,&first,&errors)
       ||!archive.loadBytes(first,"first",&errors))return {};
    QByteArray second;
    return archive.buildWithAppendedMember("CaveSet000000.SEC",sector,0,&second,&errors)
        ?second:QByteArray();
}

STerrainTransitionSignature finalizedSignature(
    const STerrainAtlasFinalization& finalization, quint16 packed, bool* found)
{
    const SMprPackedTile decoded = SMprPackedTile::decode(packed);
    const int slot = decoded.atlasSlot();
    if(slot < 0 || slot >= finalization.tiles.size())
    {
        if(found) *found = false;
        return {};
    }
    if(found) *found = true;
    return CTerrainTransitionCatalog::rotated(
        finalization.tiles.at(slot).canonicalSignature, -decoded.rotation);
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}
}

class TerrainVirtualAtlasTests:public QObject
{
    Q_OBJECT
private slots:
    void virtualFieldPaintIsUnboundedByAtlas();
    void exactAuthoredTileIsReused();
    void generatesOneThreeTuple();
    void generatesTwoTwoTuple();
    void generatesFourFamilyTupleWithOrientation();
    void generatedEdgesUseOnlyTheirTwoCornerMaterials();
    void rotationsAndIdenticalPixelsAreDeduplicated();
    void stableHashesAndNamespaceAreDeterministic();
    void mapWideUsageAndUnusedCompaction();
    void capacityRefusalNeverSubstitutes();
    void rawPageCompositionPreservesEveryUntouchedSlot();
    void pageOrientationMipmapsAlphaAndReopen();
    void namespacesAreIsolated();
    void exactOnlyNoOpPreservesPixels();
    void portablePackageRemapsNamespaceAndReopens();
    void virtualPaintPreviewAndUndoPreserveDesiredIdentity();
    void preservedLayerSlotsArePinnedAndRemapped();
    void gradedAlphaIsRefusedWithoutFlattening();
    void workingAtlasGeneratesImmediatelyAndReusesTuple();
    void workingAtlasProtectsUndoSlotsAndRefusesCapacity();
    void selfPaintPreservesExactPackedTileAndRotation();
    void parallelStripesAndBaseCrossResolveExactly();
    void nestedCircleMaterialPairingsResolveExactly();
    void generatedPaintMatrixSavesReopensAndMatchesVertexOracle();
};

void TerrainVirtualAtlasTests::virtualFieldPaintIsUnboundedByAtlas()
{
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(3,3,1));
    STerrainTileBrush brush;
    brush.centerX=1.5f; brush.centerY=1.5f; brush.radius=2.0f;
    QCOMPARE(field.paint(brush,99),12);
    QCOMPARE(field.material(1,1),qint32(99));
}

void TerrainVirtualAtlasTests::exactAuthoredTileIsReused()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images,7);
    const STerrainVirtualTile tile=atlas.resolve(signature(2,2,2,2),4,9);
    QVERIFY(tile.authored);
    QCOMPARE(SMprPackedTile::decode(tile.authoredPacked).atlasSlot(),1);
    QCOMPARE(atlas.generatedTiles().size(),0);
}

void TerrainVirtualAtlasTests::generatesOneThreeTuple()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainVirtualTile tile=atlas.resolve(signature(1,2,2,2),0,0);
    QVERIFY(!tile.authored);
    QVERIFY(tile.generatedIndex>=0);
    QCOMPARE(atlas.generatedTiles().size(),1);
}

void TerrainVirtualAtlasTests::generatesTwoTwoTuple()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    QVERIFY(atlas.resolve(signature(1,1,2,2),0,0).generatedIndex>=0);
}

void TerrainVirtualAtlasTests::generatesFourFamilyTupleWithOrientation()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainVirtualTile tile=atlas.resolve(signature(1,2,3,4),0,0);
    const QImage image=atlas.generatedTiles().at(tile.generatedIndex).pixels;
    // Grid Y is mirrored once at the image boundary: SW,NW,NE,SE become
    // preview TL,BL,BR,TR respectively.
    QCOMPARE(image.pixelColor(0,0),fixture.images[3].pixelColor(0,0));
    QCOMPARE(image.pixelColor(7,0),fixture.images[2].pixelColor(7,0));
    QCOMPARE(image.pixelColor(7,7),fixture.images[1].pixelColor(7,7));
    QCOMPARE(image.pixelColor(0,7),fixture.images[0].pixelColor(0,7));
}

void TerrainVirtualAtlasTests::generatedEdgesUseOnlyTheirTwoCornerMaterials()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainVirtualTile tile=atlas.resolve(signature(1,2,3,4),0,0);
    const QImage image=atlas.generatedTiles().at(tile.generatedIndex).pixels;
    const QColor top=image.pixelColor(3,0);
    QCOMPARE(top,QColor(140,134,106,255));
    const QColor bottom=image.pixelColor(3,7);
    QCOMPARE(bottom,QColor(134,106,20,255));
}

void TerrainVirtualAtlasTests::rotationsAndIdenticalPixelsAreDeduplicated()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainTransitionSignature base=signature(1,2,3,4);
    const STerrainVirtualTile first=atlas.resolve(base,0,0);
    const STerrainVirtualTile rotated=atlas.resolve(
        CTerrainTransitionCatalog::rotated(base,1),1,0);
    QCOMPARE(first.generatedIndex,rotated.generatedIndex);
    QVERIFY(first.generatedRotation!=rotated.generatedRotation);

    Fixture duplicate(true);
    CTerrainVirtualAtlas duplicateAtlas(duplicate.catalog,duplicate.images);
    const int a=duplicateAtlas.resolve(signature(1,2,3,3),0,0).generatedIndex;
    const int b=duplicateAtlas.resolve(signature(1,2,4,4),1,0).generatedIndex;
    QCOMPARE(a,b);
}

void TerrainVirtualAtlasTests::stableHashesAndNamespaceAreDeterministic()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1,1,1));
    field.setMaterial(1,0,2); field.setMaterial(1,1,3); field.setMaterial(0,1,4);
    CTerrainVirtualAtlas first(fixture.catalog,fixture.images,42);
    CTerrainVirtualAtlas second(fixture.catalog,fixture.images,42);
    const STerrainAtlasFinalization a=first.finalize(field,"Zone2",512);
    const STerrainAtlasFinalization b=second.finalize(field,"Zone2",512);
    QVERIFY2(a.success,qPrintable(a.error));
    QCOMPARE(a.stableSha256,b.stableSha256);
    QCOMPARE(a.isolatedNamespace,b.isolatedNamespace);
    QCOMPARE(a.pageMmpPayloads,b.pageMmpPayloads);
}

void TerrainVirtualAtlasTests::mapWideUsageAndUnusedCompaction()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(2,2,1));
    field.setMaterial(1,1,2);
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainAtlasFinalization result=atlas.finalize(field,"Zone2",512,{10,20,30,40});
    QVERIFY2(result.success,qPrintable(result.error));
    QCOMPARE(result.usage.cellCount,quint64(4));
    QVERIFY(result.requiredSlots<fixture.images.size());
    QCOMPARE(result.packedCells.size(),4);
    QCOMPARE(result.tileTypes.size(),result.requiredSlots);
}

void TerrainVirtualAtlasTests::capacityRefusalNeverSubstitutes()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(2,1,1));
    field.setMaterial(1,0,2);
    field.setMaterial(2,0,3);
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainAtlasFinalization result=atlas.finalize(field,"Zone15",1);
    QVERIFY(!result.success);
    QVERIFY(result.error.contains("exceeding"));
    QVERIFY(result.error.contains("No material identity was substituted"));
    QVERIFY(result.packedCells.isEmpty());
}

void TerrainVirtualAtlasTests::rawPageCompositionPreservesEveryUntouchedSlot()
{
    QVector<QImage> tiles;
    for(int slot = 0; slot < 64; ++slot)
    {
        QImage tile(8, 8, QImage::Format_RGBA8888);
        for(int y = 0; y < 8; ++y) for(int x = 0; x < 8; ++x)
            tile.setPixelColor(x, y, QColor(slot * 3 % 256,
                slot * 5 % 256, (x * 29 + y * 17) % 256, 255));
        tiles.append(tile);
    }
    const QImage original = CTerrainVirtualAtlas::composePage(tiles, 0, 8);
    QCOMPARE(original.size(), QSize(64, 64));
    for(int slot = 0; slot < 64; ++slot)
    {
        const int destinationX = (slot % 8) * 8;
        const int destinationY = (7 - slot / 8) * 8;
        QCOMPARE(original.copy(destinationX, destinationY, 8, 8), tiles.at(slot));
    }

    QImage replacement = tiles.at(37);
    replacement.fill(QColor(255, 0, 255, 255));
    tiles[37] = replacement;
    const QImage changed = CTerrainVirtualAtlas::composePage(tiles, 0, 8);
    for(int slot = 0; slot < 64; ++slot)
    {
        const int destinationX = (slot % 8) * 8;
        const int destinationY = (7 - slot / 8) * 8;
        const QImage before = original.copy(destinationX, destinationY, 8, 8);
        const QImage after = changed.copy(destinationX, destinationY, 8, 8);
        if(slot == 37) QVERIFY(before != after);
        else QCOMPARE(after, before);
    }
}

void TerrainVirtualAtlasTests::pageOrientationMipmapsAlphaAndReopen()
{
    Fixture fixture;
    fixture.images[0].setPixelColor(0,0,QColor(255,0,0,0));
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1,1,1));
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainAtlasFinalization result=atlas.finalize(field,"Zone1",512);
    QVERIFY2(result.success,qPrintable(result.error));
    QCOMPARE(result.pages.size(),1);
    QCOMPARE(result.pages[0].pixelColor(0,56).alpha(),0);
    const ei::SMmpInspection reopened=ei::inspectMmp(result.pageMmpPayloads[0]);
    QVERIFY2(reopened.valid,qPrintable(reopened.diagnostic));
    QCOMPARE(reopened.formatName,QString("DXT1"));
    QVERIFY(reopened.authoredMipSizes.size()>1);
    QCOMPARE(reopened.baseImage.pixelColor(0,56).alpha(),0);
}

void TerrainVirtualAtlasTests::namespacesAreIsolated()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1,1,1));
    CTerrainVirtualAtlas a(fixture.catalog,fixture.images);
    CTerrainVirtualAtlas b(fixture.catalog,fixture.images);
    const auto zone2=a.finalize(field,"Zone2",512);
    const auto gz2g=b.finalize(field,"Gz2G",512);
    QVERIFY(zone2.success&&gz2g.success);
    QVERIFY(zone2.isolatedNamespace!=gz2g.isolatedNamespace);
    QVERIFY(zone2.isolatedNamespace.startsWith("zone2_eime_"));
}

void TerrainVirtualAtlasTests::exactOnlyNoOpPreservesPixels()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(2,2,1));
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const auto result=atlas.finalize(field,"Zone1",512,{77,88,99,111});
    QVERIFY2(result.success,qPrintable(result.error));
    QCOMPARE(result.requiredSlots,1);
    QCOMPARE(result.tiles[0].pixels,fixture.images[0]);
    QCOMPARE(result.tileTypes,QVector<qint32>({77}));
    QCOMPARE(atlas.generatedTiles().size(),0);
}

void TerrainVirtualAtlasTests::portablePackageRemapsNamespaceAndReopens()
{
    CMprDocument source;CValidationReport report;
    QVERIFY(source.loadBytes(portableMpr(),"portable-source.mpr",&report));
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(16,16,1));
    field.setMaterial(8,8,2);
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const auto finalization=atlas.finalize(field,"CaveSet",512,{1,2,3,4},&report);
    QVERIFY2(finalization.success,qPrintable(finalization.error));
    const auto package=CTerrainVirtualAtlas::buildPortablePackage(
        source,finalization,ETerrainTileLayer::Land,&report);
    QVERIFY2(package.success,qPrintable(package.error+"\n"+report.toText()));
    CMprDocument reopened;
    QVERIFY(reopened.loadBytes(package.mprBytes,"portable-result.mpr",&report));
    QCOMPARE(reopened.mapInfo().innerNamespace,finalization.isolatedNamespace);
    QCOMPARE(reopened.mapInfo().textureCount,quint32(finalization.textureCount));
    QCOMPARE(reopened.mapInfo().tileCount,quint32(finalization.requiredSlots));
    CResArchiveDocument resources;QStringList errors;
    QVERIFY(resources.loadBytes(package.resourceArchiveBytes,"portable-atlas.res",&errors));
    QCOMPARE(package.resourceMembers.size(),finalization.textureCount);
    QVERIFY(!resources.memberPayload(package.resourceMembers.front()).isEmpty());
}

void TerrainVirtualAtlasTests::virtualPaintPreviewAndUndoPreserveDesiredIdentity()
{
    Fixture fixture;
    STerrainTransitionVariant authored;
    authored.signature=signature(1,1,2,1);
    SMprPackedTile authoredPacked;authoredPacked.tileIndex=10;
    authored.packedTile=authoredPacked.encode();
    QVERIFY(fixture.catalog.addVariant(authored));
    QByteArray payload;
    QDataStream sec(&payload,QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec<<quint32(0xCF4BF774)<<quint8(0);
    payload.append(QByteArray(33*33*8,'\0'));
    payload.append(QByteArray(16*16*2,'\0'));
    STerrainSectorSource source;
    source.memberName="CaveSet000000.SEC";source.sectorX=0;source.sectorY=0;
    source.type=0;source.payload=payload;source.landTileOffset=5+33*33*8;
    CTerrainEditModel model;CValidationReport report;
    QVERIFY(model.loadSources(1,1,100.0f,{source},&report,1,
                              QVector<qint32>(64,1),0));
    STerrainTileBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=1;
    const STerrainTileCommand command=model.makeVirtualMaterialPaint(
        8,8,brush,ETerrainTileLayer::Land,2,fixture.catalog,99,&report);
    QVERIFY2(!command.isEmpty(),qPrintable(report.toText()));
    QCOMPARE(command.virtualVertices.size(),5);
    QCOMPARE(command.semanticAudit.solverMode,QString("virtual-unblended-preview"));
    QVERIFY(!command.deltas.isEmpty());
    SMprPackedTile pureSelected;pureSelected.tileIndex=1;
    for(const STerrainTileDelta& delta:command.deltas)
        QCOMPARE(delta.after,pureSelected.encode());
    QVERIFY(model.apply(command,true,&report));
    CTerrainVirtualMaterialField painted;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&painted));
    for(const auto& touched:command.virtualVertices)
        QCOMPARE(painted.material(touched.globalX,touched.globalY),qint32(2));
    QCOMPARE(painted.material(6,8),qint32(1));
    QVector<QImage> workingImages = fixture.images;
    while(workingImages.size() < 64) workingImages.append(fixture.images.front());
    CTerrainWorkingAtlas working(fixture.catalog, workingImages, 99, 64);
    QMap<int,quint64> usage;
    for(int slot = 0; slot < 64; ++slot) usage.insert(slot, 0);
    QSet<int> protectedSlots({0,1,2,3,10});
    QSet<int> affected;
    for(const auto& vertex : command.virtualVertices)
        for(int y : {vertex.globalY-1, vertex.globalY})
            for(int x : {vertex.globalX-1, vertex.globalX})
                if(x >= 0 && x < painted.tileWidth() && y >= 0 && y < painted.tileHeight())
                    affected.insert(y * painted.tileWidth() + x);
    CValidationReport releaseReport;
    for(int cell : affected)
    {
        const int x = cell % painted.tileWidth();
        const int y = cell / painted.tileWidth();
        bool generated = false;
        const quint16 packed = working.resolve(painted.cellSignature(x,y), x, y,
                                               &usage, protectedSlots, &generated,
                                               &releaseReport);
        if(!releaseReport.hasErrors())
            ++usage[SMprPackedTile::decode(packed).atlasSlot()];
    }
    QVERIFY2(!releaseReport.hasErrors(), qPrintable(releaseReport.toText()));
    const STerrainTileCommand resolved=model.makeVirtualStrokeResolution(
        command,working.catalog(),99,&releaseReport);
    QVERIFY2(!releaseReport.hasErrors(), qPrintable(releaseReport.toText()));
    QCOMPARE(resolved.semanticAudit.solverMode,QString("virtual-release-exact"));
    QVERIFY(!resolved.deltas.isEmpty());
    QVERIFY(model.apply(resolved,true,&report));
    CTerrainVirtualMaterialField afterResolution;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&afterResolution));
    for(const auto& touched:command.virtualVertices)
        QCOMPARE(afterResolution.material(touched.globalX,touched.globalY),qint32(2));
    QVERIFY(model.apply(resolved,false,&report));
    QVERIFY(model.apply(command,false,&report));
    CTerrainVirtualMaterialField undone;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&undone));
    QCOMPARE(undone.material(8,8),qint32(1));
}

void TerrainVirtualAtlasTests::preservedLayerSlotsArePinnedAndRemapped()
{
    Fixture fixture;CTerrainVirtualMaterialField field;QVERIFY(field.resize(1,1,1));
    SMprPackedTile pinned;pinned.tileIndex=3;pinned.rotation=2;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const auto result=atlas.finalize(field,"Zone2",512,{10,20,30,40},nullptr,{pinned.encode()});
    QVERIFY2(result.success,qPrintable(result.error));
    QCOMPARE(result.requiredSlots,2);
    QVERIFY(result.authoredSlotRemap.contains(0));
    QVERIFY(result.authoredSlotRemap.contains(3));
    QVERIFY(result.authoredSlotRemap.value(0)!=result.authoredSlotRemap.value(3));
}

void TerrainVirtualAtlasTests::gradedAlphaIsRefusedWithoutFlattening()
{
    QImage image(8,8,QImage::Format_ARGB32);image.fill(QColor(10,20,30,128));
    QString diagnostic;
    QVERIFY(ei::encodeMmpDxt1(image,true,&diagnostic).isEmpty());
    QVERIFY(diagnostic.contains("graded alpha"));
}

void TerrainVirtualAtlasTests::workingAtlasGeneratesImmediatelyAndReusesTuple()
{
    Fixture fixture;
    CTerrainWorkingAtlas working(fixture.catalog,fixture.images,17,4);
    QVERIFY2(working.isValid(),qPrintable(working.error()));
    QMap<int,quint64> usage;
    usage[0]=4;usage[1]=4;usage[2]=4;usage[3]=0;
    QSet<int> protectedSlots({0,1,2});
    CValidationReport report;bool generated=false;
    const STerrainTransitionSignature missing=signature(1,2,2,2);
    const quint16 first=working.resolve(missing,7,8,&usage,protectedSlots,
                                        &generated,&report);
    QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));
    QVERIFY(generated);
    QCOMPARE(SMprPackedTile::decode(first).atlasSlot(),3);
    QVERIFY(working.takeChangedSlots().contains(3));
    usage[3]=1;
    const quint16 reused=working.resolve(missing,9,10,&usage,protectedSlots,
                                         &generated,&report);
    QCOMPARE(reused,first);
    QCOMPARE(working.generatedTileCount(),1);
    QVERIFY(working.takeChangedSlots().isEmpty());
    bool decoded=false;
    QCOMPARE(working.catalog().gridSignatureForPacked(reused,&decoded),missing);
    QVERIFY(decoded);
}

void TerrainVirtualAtlasTests::workingAtlasProtectsUndoSlotsAndRefusesCapacity()
{
    Fixture fixture;
    CTerrainWorkingAtlas working(fixture.catalog,fixture.images,3,4);
    QMap<int,quint64> usage;
    usage[0]=usage[1]=usage[2]=usage[3]=0;
    const QSet<int> allProtected({0,1,2,3});
    CValidationReport report;bool generated=false;
    QCOMPARE(working.resolve(signature(1,2,3,4),0,0,&usage,allProtected,
                             &generated,&report),quint16(0));
    QVERIFY(report.hasErrors());
    QVERIFY(report.toText().contains("undo history"));
    QVERIFY(report.toText().contains("No identity was substituted"));
}

void TerrainVirtualAtlasTests::selfPaintPreservesExactPackedTileAndRotation()
{
    const QVector<ETerrainTileBrushShape> shapes = {
        ETerrainTileBrushShape::Circle, ETerrainTileBrushShape::Square};
    for(qint32 material = 1; material <= 4; ++material)
        for(ETerrainTileBrushShape shape : shapes)
            for(float radius : {1.0f, 3.0f})
            {
                CTerrainTransitionCatalog catalog;
                SMprPackedTile originalPacked;
                originalPacked.tileIndex = quint8(8 + material);
                originalPacked.rotation = quint8((material + int(radius)) % 4);
                STerrainTransitionVariant variant;
                variant.signature = signature(material, material, material, material);
                variant.packedTile = originalPacked.encode();
                QVERIFY(catalog.addVariant(variant));

                QByteArray payload;
                QDataStream sec(&payload, QIODevice::WriteOnly);
                sec.setByteOrder(QDataStream::LittleEndian);
                sec << quint32(0xCF4BF774) << quint8(0);
                payload.append(QByteArray(33 * 33 * 8, '\0'));
                const int tileOffset = payload.size();
                payload.append(QByteArray(16 * 16 * 2, '\0'));
                for(int cell = 0; cell < 16 * 16; ++cell)
                    qToLittleEndian<quint16>(originalPacked.encode(),
                        reinterpret_cast<uchar*>(payload.data() + tileOffset + cell * 2));
                STerrainSectorSource source;
                source.memberName = "Self000000.SEC";
                source.sectorX = source.sectorY = 0;
                source.type = 0;
                source.payload = payload;
                source.landTileOffset = tileOffset;
                CTerrainEditModel model;
                CValidationReport report;
                QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report,
                                          1, QVector<qint32>(64, 1), 0));
                STerrainTileBrush brush;
                brush.centerX = brush.centerY = 8.0f;
                brush.radius = radius;
                brush.shape = shape;
                const STerrainTileCommand command = model.makeVirtualMaterialPaint(
                    8, 8, brush, ETerrainTileLayer::Land, material,
                    catalog, 0x2468u, &report);
                QVERIFY2(command.isEmpty(), qPrintable(report.toText()));
                for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
                {
                    bool available = false;
                    QCOMPARE(model.packedTile(x, y, ETerrainTileLayer::Land,
                                              &available), originalPacked.encode());
                    QVERIFY(available);
                }
            }
}

void TerrainVirtualAtlasTests::parallelStripesAndBaseCrossResolveExactly()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(16, 16, 1));
    for(int stripe = 0; stripe < 3; ++stripe)
        for(int y = 2 + stripe * 5; y <= 4 + stripe * 5; ++y)
            for(int x = 2; x <= 14; ++x)
                QVERIFY(field.setMaterial(x, y, stripe + 2));
    for(int y = 1; y <= 15; ++y)
        for(int x = 7; x <= 9; ++x)
            QVERIFY(field.setMaterial(x, y, 1));

    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x314159u);
    CValidationReport report;
    const auto finalized = atlas.finalize(field, "StripeCross", 512,
                                           {1,2,3,4}, &report);
    QVERIFY2(finalized.success, qPrintable(finalized.error + "\n" + report.toText()));
    for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
    {
        bool decoded = false;
        QCOMPARE(finalizedSignature(finalized,
            finalized.packedCells.at(y * 16 + x), &decoded), field.cellSignature(x, y));
        QVERIFY(decoded);
    }
    for(int y = 1; y <= 15; ++y)
        for(int x = 7; x <= 9; ++x)
            QCOMPARE(field.material(x, y), qint32(1));

    CMprDocument source;
    QVERIFY(source.loadBytes(portableMpr(), "stripe-source.mpr", &report));
    const auto package = CTerrainVirtualAtlas::buildPortablePackage(
        source, finalized, ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success, qPrintable(package.error + "\n" + report.toText()));
    CMprDocument reopened;
    QVERIFY(reopened.loadBytes(package.mprBytes, "stripe-cross.mpr", &report));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(reopened, &report));
    for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
    {
        bool available = false;
        QCOMPARE(terrain.packedTile(x, y, ETerrainTileLayer::Land, &available),
                 finalized.packedCells.at(y * 16 + x));
        QVERIFY(available);
    }
}

void TerrainVirtualAtlasTests::nestedCircleMaterialPairingsResolveExactly()
{
    Fixture fixture;
    int scenarios = 0;
    for(qint32 outer = 1; outer <= 4; ++outer)
        for(qint32 inner = 1; inner <= 4; ++inner)
        {
            if(inner == outer) continue;
            CTerrainVirtualMaterialField field;
            QVERIFY(field.resize(16, 16, 1));
            STerrainTileBrush outerBrush;
            outerBrush.centerX = outerBrush.centerY = 8.0f;
            outerBrush.radius = 3.0f;
            outerBrush.shape = ETerrainTileBrushShape::Circle;
            const int outerChanges = field.paint(outerBrush, outer);
            if(outer == 1) QCOMPARE(outerChanges, 0);
            else QVERIFY(outerChanges > 0);
            STerrainTileBrush innerBrush = outerBrush;
            innerBrush.radius = 1.0f;
            QVERIFY(field.paint(innerBrush, inner) > 0);

            CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x271828u);
            CValidationReport report;
            const auto finalized = atlas.finalize(field,
                QString("Nested-%1-%2").arg(outer).arg(inner), 512,
                {1,2,3,4}, &report);
            QVERIFY2(finalized.success,
                qPrintable(finalized.error + "\n" + report.toText()));
            for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
            {
                bool decoded = false;
                QCOMPARE(finalizedSignature(finalized,
                    finalized.packedCells.at(y * 16 + x), &decoded),
                    field.cellSignature(x, y));
                QVERIFY(decoded);
            }
            CMprDocument source;
            QVERIFY(source.loadBytes(portableMpr(), "nested-source.mpr", &report));
            const auto package = CTerrainVirtualAtlas::buildPortablePackage(
                source, finalized, ETerrainTileLayer::Land, &report);
            QVERIFY2(package.success,
                qPrintable(package.error + "\n" + report.toText()));
            CMprDocument reopened;
            QVERIFY(reopened.loadBytes(package.mprBytes,
                QString("nested-%1-%2.mpr").arg(outer).arg(inner), &report));
            CTerrainEditModel terrain;
            QVERIFY(terrain.load(reopened, &report));
            for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
            {
                bool available = false;
                QCOMPARE(terrain.packedTile(x, y, ETerrainTileLayer::Land,
                                            &available),
                         finalized.packedCells.at(y * 16 + x));
                QVERIFY(available);
            }
            CTerrainVirtualAtlas repeated(fixture.catalog, fixture.images, 0x271828u);
            const auto again = repeated.finalize(field,
                QString("Nested-%1-%2").arg(outer).arg(inner), 512, {1,2,3,4});
            QCOMPARE(again.stableSha256, finalized.stableSha256);
            QCOMPARE(again.pageMmpPayloads, finalized.pageMmpPayloads);
            ++scenarios;
        }
    QCOMPARE(scenarios, 12);
}

void TerrainVirtualAtlasTests::generatedPaintMatrixSavesReopensAndMatchesVertexOracle()
{
    struct Scenario { QString name; qint32 background; qint32 brush; QVector<QPoint> touched; };
    QVector<Scenario> scenarios;
    const QVector<QString> shapes = {"square", "horizontal", "vertical", "diagonal-nw-se", "diagonal-ne-sw"};
    for(qint32 background = 1; background <= 4; ++background)
        for(qint32 brush = 1; brush <= 4; ++brush)
        {
            for(const QString& shape : shapes)
            {
                Scenario scenario;
                scenario.name = QString("pair-%1-over-%2-%3").arg(brush).arg(background).arg(shape);
                scenario.background = background;
                scenario.brush = brush;
                if(shape == "square")
                    for(int y = 7; y <= 9; ++y) for(int x = 7; x <= 9; ++x)
                        scenario.touched.append(QPoint(x, y));
                else
                    for(int offset = -3; offset <= 3; ++offset)
                    {
                        if(shape == "horizontal") scenario.touched.append(QPoint(8 + offset, 8));
                        else if(shape == "vertical") scenario.touched.append(QPoint(8, 8 + offset));
                        else if(shape == "diagonal-nw-se") scenario.touched.append(QPoint(8 + offset, 8 + offset));
                        else scenario.touched.append(QPoint(8 + offset, 8 - offset));
                    }
                scenarios.append(scenario);
            }
        }
    Scenario threeFamily{"junction-three-family", 1, 2,
        {QPoint(8,8), QPoint(9,8), QPoint(8,9)}};
    scenarios.append(threeFamily);
    Scenario fourFamily{"junction-four-family", 1, 2,
        {QPoint(8,8), QPoint(9,8), QPoint(9,9), QPoint(8,9)}};
    scenarios.append(fourFamily);

    Fixture fixture;
    CMprDocument source;
    CValidationReport sourceReport;
    QVERIFY2(source.loadBytes(portableMpr(), "matrix-source.mpr", &sourceReport),
             qPrintable(sourceReport.toText()));
    const QString outputPath = QString::fromLocal8Bit(qgetenv("EI_MAPPER_ATLAS_TEST_OUTPUT"));
    if(!outputPath.isEmpty()) QVERIFY(QDir().mkpath(outputPath));
    QStringList rawResults;
    int authoredCells = 0;
    int generatedCells = 0;

    for(const Scenario& scenario : scenarios)
    {
        CTerrainVirtualMaterialField field;
        QVERIFY(field.resize(16, 16, scenario.background));
        for(const QPoint& point : scenario.touched)
            QVERIFY(field.setMaterial(point.x(), point.y(), scenario.brush));
        if(scenario.name == "junction-three-family")
        {
            QVERIFY(field.setMaterial(8,8,2));
            QVERIFY(field.setMaterial(9,8,3));
            QVERIFY(field.setMaterial(8,9,2));
        }
        else if(scenario.name == "junction-four-family")
        {
            QVERIFY(field.setMaterial(8,8,1));
            QVERIFY(field.setMaterial(9,8,2));
            QVERIFY(field.setMaterial(9,9,3));
            QVERIFY(field.setMaterial(8,9,4));
        }
        for(const QPoint& point : scenario.touched)
            QVERIFY(field.material(point.x(), point.y()) != 0);

        CValidationReport report;
        CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x13579u);
        const auto finalized = atlas.finalize(field, "MatrixMap", 512,
                                               {1,2,3,4}, &report);
        QVERIFY2(finalized.success, qPrintable(finalized.error + "\n" + report.toText()));
        const auto package = CTerrainVirtualAtlas::buildPortablePackage(
            source, finalized, ETerrainTileLayer::Land, &report);
        QVERIFY2(package.success, qPrintable(package.error + "\n" + report.toText()));

        for(int y = 0; y < 16; ++y)
            for(int x = 0; x < 16; ++x)
            {
                const int cell = y * 16 + x;
                bool decoded = false;
                const auto actual = finalizedSignature(
                    finalized, finalized.packedCells.at(cell), &decoded);
                QVERIFY(decoded);
                QCOMPARE(actual, field.cellSignature(x, y));
                const SMprPackedTile packed = SMprPackedTile::decode(
                    finalized.packedCells.at(cell));
                if(finalized.tiles.at(packed.atlasSlot()).authored) ++authoredCells;
                else ++generatedCells;
                if(x + 1 < 16)
                {
                    const auto east = finalizedSignature(
                        finalized, finalized.packedCells.at(cell + 1), &decoded);
                    QCOMPARE(actual.northEast, east.northWest);
                    QCOMPARE(actual.southEast, east.southWest);
                }
                if(y + 1 < 16)
                {
                    const auto south = finalizedSignature(
                        finalized, finalized.packedCells.at(cell + 16), &decoded);
                    QCOMPARE(actual.southWest, south.northWest);
                    QCOMPARE(actual.southEast, south.northEast);
                }
            }

        CMprDocument reopened;
        QVERIFY2(reopened.loadBytes(package.mprBytes, scenario.name + ".mpr", &report),
                 qPrintable(report.toText()));
        CTerrainEditModel reopenedTerrain;
        QVERIFY2(reopenedTerrain.load(reopened, &report), qPrintable(report.toText()));
        for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
        {
            bool available = false;
            const quint16 packed = reopenedTerrain.packedTile(
                x, y, ETerrainTileLayer::Land, &available);
            QVERIFY(available);
            QCOMPARE(packed, finalized.packedCells.at(y * 16 + x));
            bool decoded = false;
            QCOMPARE(finalizedSignature(finalized, packed, &decoded),
                     field.cellSignature(x, y));
            QVERIFY(decoded);
        }

        CTerrainVirtualAtlas repeatAtlas(fixture.catalog, fixture.images, 0x13579u);
        const auto repeat = repeatAtlas.finalize(field, "MatrixMap", 512, {1,2,3,4});
        QCOMPARE(repeat.stableSha256, finalized.stableSha256);
        QCOMPARE(repeat.packedCells, finalized.packedCells);
        QCOMPARE(repeat.pageMmpPayloads, finalized.pageMmpPayloads);
        rawResults.append(QString("PASS %1 hash=%2 slots=%3 generated=%4")
            .arg(scenario.name, QString::fromLatin1(finalized.stableSha256))
            .arg(finalized.requiredSlots).arg(finalized.usage.generatedTiles.size()));
        if(!outputPath.isEmpty())
        {
            const QDir output(outputPath);
            QVERIFY(writeBytes(output.filePath(scenario.name + ".mpr"), package.mprBytes));
            QVERIFY(writeBytes(output.filePath(scenario.name + "-atlas.res"),
                               package.resourceArchiveBytes));
        }
    }
    QCOMPARE(scenarios.size(), 82);
    QVERIFY(authoredCells > 0);
    QVERIFY(generatedCells > 0);
    rawResults.prepend(QString("PASS scenarios=%1 authoredCells=%2 generatedCells=%3")
        .arg(scenarios.size()).arg(authoredCells).arg(generatedCells));
    if(!outputPath.isEmpty())
        QVERIFY(writeBytes(QDir(outputPath).filePath("MODERN_ATLAS_AUTOTEST_RESULTS.txt"),
                           rawResults.join('\n').append('\n').toUtf8()));
}

QTEST_GUILESS_MAIN(TerrainVirtualAtlasTests)
#include "terrain_virtual_atlas_tests.moc"
