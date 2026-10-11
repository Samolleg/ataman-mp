#include <QtTest>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QtEndian>
#include <QDataStream>
#include <QTransform>
#include "terrain_hd_republish.h"
#include "terrain_virtual_atlas.h"
#include "mmp_codec.h"
#include "mpr_document.h"
#include "endurance_document.h"
#include "terrain_semantic_field_store.h"
#include "terrain_mixed_edge.h"
#include <QCryptographicHash>
#include "terrain_hd_progress_dialog.h"
#include "verified_clipboard.h"
#include <QtConcurrent>
#include "ui_theme.h"
#include <QStyleFactory>

namespace {
QImage pattern(int n)
{
    QImage image(256,256,QImage::Format_RGBA8888);
    for(int y=0;y<256;++y)for(int x=0;x<256;++x)
        image.setPixel(x,y,qRgb(n?25+(x%17):130+(x%17),n?130+(y%17):35+(y%17),30+(x+y)%31));
    return image;
}
QByteArray lowMip(const QByteArray& hd)
{
    QByteArray low=hd.left(76)+hd.mid(76+2048*2048/2);
    qToLittleEndian<quint32>(1024,reinterpret_cast<uchar*>(low.data()+4));
    qToLittleEndian<quint32>(1024,reinterpret_cast<uchar*>(low.data()+8));
    qToLittleEndian<quint32>(11,reinterpret_cast<uchar*>(low.data()+12));return low;
}
STerrainAtlasProfile profile(const QVector<QImage>& images)
{
    STerrainAtlasProfile p;p.mprNamespace="published";p.approved=true;p.atlasSlotCount=64;p.textureCount=1;
    p.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(images);
    for(int id=1;id<=2;++id){STerrainAtlasProfileFamily f;f.id=id;f.name=id==1?"Grass":"Soil";f.tileType=id;p.families.append(f);}
    for(int slot=0;slot<64;++slot){STerrainAtlasProfileSlot d;d.slot=slot;const int id=slot==1?2:1;
        d.signature.northWest=d.signature.northEast=d.signature.southEast=d.signature.southWest=id;p.slotDefinitions.append(d);}
    return p;
}
QByteArray mapBytes()
{
    QByteArray mp;QDataStream out(&mp,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out<<quint32(0xCE4AF672)<<float(100)<<quint32(1)<<quint32(1)<<quint32(1)<<quint32(1024)<<quint32(64)<<quint32(128)<<quint16(0)<<quint32(1);
    for(int i=0;i<64;++i)out<<qint32(i==1?2:1);out<<quint16(60)<<quint16(4);
    QByteArray sector;QDataStream sec(&sector,QIODevice::WriteOnly);sec.setByteOrder(QDataStream::LittleEndian);sec<<quint32(0xCF4BF774)<<quint8(0);
    sector.append(QByteArray(33*33*8+16*16*2,'\0'));
    for(int i=0;i<256;++i){SMprPackedTile t;t.tileIndex=i%64;t.rotation=i%4;qToLittleEndian<quint16>(t.encode(),reinterpret_cast<uchar*>(sector.data()+5+33*33*8+i*2));}
    QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
    CResArchiveDocument res;res.loadBytes(empty);QByteArray bytes;SResArchiveNewMember a,b;a.name="published.mp";a.payload=mp;b.name="published000000.sec";b.payload=sector;
    res.buildWithAppendedMembers({a,b},&bytes);return bytes;
}
}
class TerrainHdRepublishTests:public QObject
{
    Q_OBJECT
private slots:
    void progressCacheAndCancellation()
    {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        QVector<QImage> tiles(64,pattern(0));tiles[1]=pattern(1);
        const auto hd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,256),true,nullptr);
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QVERIFY(archive.loadBytes(empty));SResArchiveNewMember member;member.name="stock000.mmp";member.payload=hd;
        QByteArray res;QVERIFY(archive.buildWithAppendedMembers({member},&res));
        const QString path=dir.path()+"/stock.res",cache=dir.path()+"/cache";
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(res),qint64(res.size()));file.close();
        const auto sd=lowMip(hd);QVector<QImage> before;
        for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,i,&image));before.append(image);}
        const auto p=profile(before);QString error;
        CMprDocument map;CValidationReport report;QVERIFY(map.loadBytes(mapBytes(),"test.mpr",&report));
        for(int stage=CTerrainHdProgress::Opening;stage<=CTerrainHdProgress::Preparing;++stage) {
            bool reached=false;int last=0;
            CTerrainHdProgress progress([&](const CTerrainHdProgress::Snapshot& s) {
                QVERIFY(s.percent>=last);last=s.percent;
                if(int(s.stage)==stage&&(stage!=CTerrainHdProgress::Indexing||s.done>=8)) {reached=true;progress.cancel();}
            });
            CTerrainHdSources sources;
            if(sources.open({path},&error,&progress,cache)) {
                const auto result=rebuildTerrainAtlasHd(p,{sd},sources,true,{}, {},&progress);
                QVERIFY(result.cancelled);QVERIFY(!result.success);QVERIFY(result.publishedMembers.isEmpty());QVERIFY(result.mprBytes.isEmpty());
            }
            QVERIFY(reached);QVERIFY(progress.cancelled());QVERIFY(!sources.saveCache());
            QCOMPARE(QDir(dir.path()).entryList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot),QStringList{"stock.res"});
            QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),res);file.close();
        }
        int last=0;QSet<int> stages;
        CTerrainHdProgress progress([&](const CTerrainHdProgress::Snapshot& s){QVERIFY(s.percent>=last);QVERIFY(s.percent<=100);last=s.percent;stages.insert(s.stage);});
        CTerrainHdSources first;QVERIFY(first.open({path},&error,&progress,cache));QVERIFY(!first.cacheHit());
        auto result=rebuildTerrainAtlasHd(p,{sd},first,true,{}, {},&progress);QVERIFY(result.success);
        QVERIFY(prepareTerrainHdPublication(map,{},&result));progress.update(CTerrainHdProgress::Complete,1,1);
        QCOMPARE(last,100);QCOMPARE(stages.size(),6);QVERIFY(first.saveCache());
        CTerrainHdSources cached;QVERIFY(cached.open({path},&error,nullptr,cache));QVERIFY(cached.cacheHit());
        auto again=rebuildTerrainAtlasHd(p,{sd},cached);QVERIFY(prepareTerrainHdPublication(map,{},&again));
        QCOMPARE(again.pages,result.pages);QCOMPARE(again.mprBytes,result.mprBytes);QCOMPARE(again.profile.toBytes(),result.profile.toBytes());
        QCOMPARE(again.receipt,result.receipt);QCOMPARE(again.publishedMembers.size(),result.publishedMembers.size());
        for(int i=0;i<again.publishedMembers.size();++i)QCOMPARE(again.publishedMembers[i].payload,result.publishedMembers[i].payload);
        // A corrupt cache is ignored; source content is still authoritative.
        const auto entries=QDir(cache).entryList({"*.cache"},QDir::Files);QCOMPARE(entries.size(),1);
        QFile broken(cache+"/"+entries.first());QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("broken");broken.close();
        CTerrainHdSources fallback;QVERIFY(fallback.open({path},&error,nullptr,cache));QVERIFY(!fallback.cacheHit());QVERIFY(fallback.saveCache());
        // Invalidate even when size and mtime stay identical (full SHA key).
        const auto stamp=QFileInfo(path).lastModified();QVERIFY(file.open(QIODevice::ReadWrite));
        const auto changedOffset=res.indexOf(hd)+hd.size()-1;QVERIFY(changedOffset>=0);QVERIFY(file.seek(changedOffset));char byte=0;QCOMPARE(file.read(&byte,1),qint64(1));byte^=1;QVERIFY(file.seek(changedOffset));QCOMPARE(file.write(&byte,1),qint64(1));
        QVERIFY(file.setFileTime(stamp,QFileDevice::FileModificationTime));file.close();QCOMPARE(QFileInfo(path).lastModified(),stamp);
        CTerrainHdSources changed;QVERIFY(changed.open({path},&error,nullptr,cache));QVERIFY(!changed.cacheHit());
    }
    void progressDialogResponsiveAndCancel()
    {
        qApp->setStyle(QStyleFactory::create("Fusion"));
        ei::installDarkUiPalette();
        CTerrainHdProgress progress;CTerrainHdProgressDialog dialog(progress);
        dialog.show();
        auto future=QtConcurrent::run([&](){for(int i=0;i<=1000&&!progress.cancelled();++i){progress.update(CTerrainHdProgress::Recovering,i,1000);QThread::msleep(10);}});
        QTRY_VERIFY(dialog.findChild<QProgressBar*>()->value()>55);
        auto label=dialog.findChild<QLabel*>("hdProgressLabel");QVERIFY(label);QVERIFY(label->text().contains(QStringLiteral("Восстановление переходов")));
        QTest::qWait(3200);dialog.refresh();QVERIFY(progress.snapshot().stageRemainingMs>0);
        const QString png=qEnvironmentVariable("EI_HD_PROGRESS_PNG");
        if(!png.isEmpty())QVERIFY(dialog.grab().save(png,"PNG"));
        QTest::mouseClick(dialog.findChild<QPushButton*>("hdProgressCancel"),Qt::LeftButton);
        QVERIFY(progress.cancelled());QVERIFY(!dialog.findChild<QPushButton*>("hdProgressCancel")->isEnabled());
        future.waitForFinished();QVERIFY(dialog.isVisible());dialog.accept();
        CTerrainHdProgress escape;CTerrainHdProgressDialog escaped(escape);escaped.show();QTest::keyClick(&escaped,Qt::Key_Escape);QVERIFY(escape.cancelled());escaped.accept();
    }
    void originalBlocksAndDeterminism()
    {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        QVector<QImage> tiles(64,pattern(0));tiles[1]=pattern(1);
        const auto hd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,256),true,nullptr);
        QVERIFY(!hd.isEmpty());
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QVERIFY(archive.loadBytes(empty));SResArchiveNewMember member;member.name="stock000.mmp";member.payload=hd;
        QByteArray res;QVERIFY(archive.buildWithAppendedMembers({member},&res));
        const QString path=dir.path()+"/stock.res";QSaveFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(res),qint64(res.size()));QVERIFY(file.commit());
        CTerrainHdSources sources;QString error;QVERIFY2(sources.open({path},&error),qPrintable(error));
        const QByteArray sd=lowMip(hd);QVector<QImage> before;
        for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,slot,&image));before.append(image);}
        auto p=profile(before);const auto first=rebuildTerrainAtlasHd(p,{sd},sources);
        QVERIFY2(first.success,qPrintable(first.error));QCOMPARE(first.receipt["hdApplied"].toInt(),64);QCOMPARE(first.receipt["retained128"].toInt(),0);
        // All slots come from this same page: exclusively owned mip blocks
        // must be its original BC1 bytes, not merely similar decoded pixels.
        QCOMPARE(first.pages[0].mid(76,2048*2048/2),hd.mid(76,2048*2048/2));
        const auto repeat=rebuildTerrainAtlasHd(p,{sd},sources);QCOMPARE(repeat.pages,first.pages);QCOMPARE(repeat.profile.toBytes(),first.profile.toBytes());
        CMprDocument map;CValidationReport report;QVERIFY(map.loadBytes(mapBytes(),"test.mpr",&report));
        // Real published maps have fewer MP class records than physical page slots.
        CMprEditTransaction shorten;QByteArray shortMp=map.memberPayload(map.mapInfo().memberName);
        shortMp.remove(38+61*4,3*4);qToLittleEndian<quint32>(61,reinterpret_cast<uchar*>(shortMp.data()+24));
        qToLittleEndian<quint16>(1,reinterpret_cast<uchar*>(shortMp.data()+38+61*4+2));
        for(const auto& member:map.members())if(member.name==map.mapInfo().memberName)shorten.replaceMemberPayload(member.name,member.sha256,shortMp);
        const auto sectorInfo=map.sectors()[0];const auto sectorMember=map.members()[sectorInfo.memberIndex];QByteArray shortSec=sectorMember.payload;
        for(int cell=0;cell<256;++cell){uchar* at=reinterpret_cast<uchar*>(shortSec.data()+sectorInfo.landTileOffset+cell*2);auto tile=SMprPackedTile::decode(qFromLittleEndian<quint16>(at));if(tile.atlasSlot()>=61){tile.tileIndex=0;qToLittleEndian<quint16>(tile.encode(),at);}}
        shorten.replaceMemberPayload(sectorMember.name,sectorMember.sha256,shortSec);QByteArray shortBytes;QVERIFY(map.applyTransaction(shorten,&shortBytes,&report));
        CMprDocument shortMap;QVERIFY(shortMap.loadBytes(shortBytes,"short.mpr",&report));auto shortResult=first;
        QVERIFY2(prepareTerrainHdPublication(shortMap,{},&shortResult),qPrintable(shortResult.error));
        CMprDocument paddedMap;QVERIFY(paddedMap.loadBytes(shortResult.mprBytes,"padded.mpr",&report));
        QCOMPARE(paddedMap.mapInfo().tileTypes.mid(0,61),shortMap.mapInfo().tileTypes);
        auto candidate=first;QVERIFY2(prepareTerrainHdPublication(map,{},&candidate),qPrintable(candidate.error));
        QVERIFY(QRegularExpression("^[a-zA-Z0-9_]+_[0-9a-f]{12}$").match(candidate.profile.mprNamespace).hasMatch());
        CMprDocument reopened;QVERIFY(reopened.loadBytes(candidate.mprBytes,"candidate.mpr",&report));
        auto legacyRecovery=candidate.receipt;legacyRecovery.remove("outputSlots");
        auto legacyRows=legacyRecovery["slots"].toArray();
        for(int i=0;i<legacyRows.size();++i){auto row=legacyRows[i].toObject();row.remove("outputSlot");legacyRows[i]=row;}legacyRecovery["slots"]=legacyRows;
        auto legacyPages=legacyRecovery["pages"].toArray();
        for(int i=0;i<legacyPages.size();++i){auto page=legacyPages[i].toObject();page.remove("tileSize");legacyPages[i]=page;}legacyRecovery["pages"]=legacyPages;
        const auto legacyAgain=rebuildTerrainAtlasHd(candidate.profile,candidate.pages,sources,true,reopened.mapInfo().animatedTiles,QJsonDocument(legacyRecovery).toJson());
        QVERIFY2(legacyAgain.success,qPrintable(legacyAgain.error));QCOMPARE(legacyAgain.pages,candidate.pages);
        QCOMPARE(reopened.mapInfo().tileTypes,map.mapInfo().tileTypes);QCOMPARE(reopened.mapInfo().animatedTiles,map.mapInfo().animatedTiles);
        QCOMPARE(reopened.sectors().size(),map.sectors().size());
        QCOMPARE(reopened.members()[reopened.sectors()[0].memberIndex].payload,map.members()[map.sectors()[0].memberIndex].payload);
        STerrainSemanticFieldDocument field;field.mprNamespace="published";field.vertexWidth=field.vertexHeight=17;
        field.land=QVector<qint32>(289,1);field.overlay=QVector<qint32>(289,0);
        field.atlasPixelSha256=QByteArray::fromHex(p.atlasPixelSha256);field.profileSha256=QCryptographicHash::hash(p.toBytes(),QCryptographicHash::Sha256);
        field.terrainTilesSha256=STerrainSemanticFieldDocument::tileBindingSha256(map);field.hasRandomPaintSeed=true;field.randomPaintSeed=12345;
        auto withSemantic=first;QVERIFY2(prepareTerrainHdPublication(map,field.toBytes(),&withSemantic),qPrintable(withSemantic.error));
        CMprDocument semanticMap;QVERIFY(semanticMap.loadBytes(withSemantic.mprBytes,"semantic.mpr",&report));STerrainSemanticFieldDocument migrated;
        QVERIFY(STerrainSemanticFieldDocument::fromBytes(semanticMap.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&migrated));
        QCOMPARE(migrated.land,field.land);QCOMPARE(migrated.overlay,field.overlay);QCOMPARE(migrated.randomPaintSeed,field.randomPaintSeed);
        QVERIFY(migrated.validateBinding(withSemantic.profile.mprNamespace,{},QByteArray::fromHex(withSemantic.profile.atlasPixelSha256),QCryptographicHash::hash(withSemantic.profile.toBytes(),QCryptographicHash::Sha256),17,17,&report,STerrainSemanticFieldDocument::tileBindingSha256(semanticMap)));
        auto repeatSemantic=repeat;QVERIFY(prepareTerrainHdPublication(map,field.toBytes(),&repeatSemantic));QCOMPARE(repeatSemantic.mprBytes,withSemantic.mprBytes);
        // Recompressed rotations are matched physically; SEC rotations never change.
        QVector<QImage> turned=before;turned[3]=before[0].transformed(QTransform().rotate(90));
        const auto turnedMmp=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(turned,0,128),true,nullptr);
        QVector<QImage> turnedDecoded;for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(turnedMmp,i,&image));turnedDecoded.append(image);}
        const auto turnedHd=rebuildTerrainAtlasHd(profile(turnedDecoded),{turnedMmp},sources);
        QVERIFY(turnedHd.success);QCOMPARE(turnedHd.receipt["hdApplied"].toInt(),64);
        QCOMPARE(turnedHd.after[3],first.after[0].transformed(QTransform().rotate(90)));
        QImage unrelated(128,128,QImage::Format_RGBA8888);unrelated.fill(Qt::blue);
        CTerrainHdSources::Source wrong;QString reason;QJsonObject evidence;
        auto invalid=p.slotDefinitions[0];invalid.sourceNamespace="stock";invalid.sourceSlotAliases={0};invalid.sourcePixelSha256=p.atlasPixelSha256;
        QVERIFY(!sources.match(unrelated,invalid,&wrong,&reason,&evidence));
        auto partial=before;partial[3]=unrelated;
        const auto partialSd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(partial,0,128),true,nullptr);
        QVector<QImage> partialDecoded;for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(partialSd,i,&image));partialDecoded.append(image);}
        const auto carried=rebuildTerrainAtlasHd(profile(partialDecoded),{partialSd},sources);
        QVERIFY2(carried.success,qPrintable(carried.error));QCOMPARE(carried.receipt["hdApplied"].toInt(),63);QCOMPARE(carried.receipt["retained128"].toInt(),1);QCOMPARE(carried.receipt["sdCarried256"].toInt(),0);
        QCOMPARE(carried.after[carried.slotRemap[3]],partialDecoded[3]);
        QCOMPARE(carried.after[carried.slotRemap[3]].width(),128);
        QCOMPARE(carried.pages.size(),2);
        STerrainAtlasProfile portable;QVERIFY2(STerrainAtlasProfile::fromBytes(carried.profile.toBytes(),&portable,&report),qPrintable(report.toText()));
        QCOMPARE(portable.atlasSlotCount,128);QCOMPARE(portable.slotDefinitions.size(),128);
        QCOMPARE(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(carried.pages[1].constData()+4)),quint32(1024));
        QVERIFY(carried.pages[0].size()+carried.pages[1].size()<2*hd.size());
        auto remapped=carried;QVERIFY2(prepareTerrainHdPublication(map,{},&remapped),qPrintable(remapped.error));
        CMprDocument moved;QVERIFY(moved.loadBytes(remapped.mprBytes,"moved.mpr",&report));
        QCOMPARE(moved.mapInfo().tileTypes[carried.slotRemap[1]],map.mapInfo().tileTypes[1]);
        QCOMPARE(moved.mapInfo().animatedTiles[0].tileIndex,quint16(carried.slotRemap[60]));
        auto remappedField=field;
        remappedField.atlasPixelSha256=QByteArray::fromHex(profile(partialDecoded).atlasPixelSha256);
        remappedField.profileSha256=QCryptographicHash::hash(profile(partialDecoded).toBytes(),QCryptographicHash::Sha256);
        auto semanticRemap=carried;QVERIFY2(prepareTerrainHdPublication(map,remappedField.toBytes(),&semanticRemap),qPrintable(semanticRemap.error));
        CMprDocument semanticMoved;QVERIFY(semanticMoved.loadBytes(semanticRemap.mprBytes,"repacked-semantic.mpr",&report));
        STerrainSemanticFieldDocument remappedSaved;
        QVERIFY(STerrainSemanticFieldDocument::fromBytes(semanticMoved.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&remappedSaved));
        QCOMPARE(remappedSaved.land,field.land);QCOMPARE(remappedSaved.overlay,field.overlay);QCOMPARE(remappedSaved.randomPaintSeed,field.randomPaintSeed);
        QVERIFY(remappedSaved.validateBinding(semanticRemap.profile.mprNamespace,{},QByteArray::fromHex(semanticRemap.profile.atlasPixelSha256),QCryptographicHash::hash(semanticRemap.profile.toBytes(),QCryptographicHash::Sha256),17,17,&report,STerrainSemanticFieldDocument::tileBindingSha256(semanticMoved)));

        QVERIFY(remapped.receipt["SECReferencesEquivalent"].toBool());
        QVERIFY(remapped.receipt["remappedCells"].toInt()>0);
        const auto oldSec=map.sectors()[0],newSec=moved.sectors()[0];
        const auto oldPayload=map.members()[oldSec.memberIndex].payload,newPayload=moved.members()[newSec.memberIndex].payload;
        for(int cell=0;cell<256;++cell) {
            const auto a=SMprPackedTile::decode(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(oldPayload.constData()+oldSec.landTileOffset+cell*2)));
            const auto b=SMprPackedTile::decode(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(newPayload.constData()+newSec.landTileOffset+cell*2)));
            QCOMPARE(b.atlasSlot(),carried.slotRemap[a.atlasSlot()]);QCOMPARE(b.rotation,a.rotation);
        }
        const auto recovery=QJsonDocument(remapped.receipt).toJson(QJsonDocument::Compact);
        const auto again=rebuildTerrainAtlasHd(remapped.profile,remapped.pages,sources,true,moved.mapInfo().animatedTiles,recovery);
        QVERIFY2(again.success,qPrintable(again.error));QCOMPARE(again.pages,remapped.pages);
        QCOMPARE(again.receipt["nativeHdSourceRecovered"].toInt(),63);
        CTerrainHdSources::Source nativeSource;
        QVERIFY(sources.matchNativeHd(first.after[0],before[0],&nativeSource));
        QCOMPARE(sources.image(nativeSource,128),before[0]);
        QVERIFY(!sources.matchNativeHd(first.after[0],partialDecoded[3],&nativeSource));
        const auto order=again.receipt["sourceOrder"].toArray();QCOMPARE(order.size(),128);
        for(int i=0;i<64;++i)QCOMPARE(order[i].toInt(),carried.slotRemap[i]);
        auto brokenRecovery=remapped.receipt;auto rows=brokenRecovery["slots"].toArray();rows[1]=rows[0];brokenRecovery["slots"]=rows;
        QVERIFY(!rebuildTerrainAtlasHd(remapped.profile,remapped.pages,sources,true,moved.mapInfo().animatedTiles,QJsonDocument(brokenRecovery).toJson()).success);
        auto noOp=again;QVERIFY(prepareTerrainHdPublication(moved,{},&noOp));
        QCOMPARE(noOp.mprBytes,remapped.mprBytes);QVERIFY(noOp.publishedMembers.isEmpty());
        const auto third=rebuildTerrainAtlasHd(remapped.profile,remapped.pages,sources,true,moved.mapInfo().animatedTiles,QJsonDocument(noOp.receipt).toJson());
        QVERIFY2(third.success,qPrintable(third.error));QCOMPARE(third.pages,remapped.pages);
        const QString root=dir.path()+"/build-tests-cli";QVERIFY(QDir().mkpath(root+"/maps"));QVERIFY(QDir().mkpath(root+"/inputs"));
        auto saveCopy=[](const QString& path,const QByteArray& data){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();};
        QByteArray publishedArchive;QVERIFY(archive.buildWithPublishedMembers(remapped.publishedMembers,false,&publishedArchive));
        QVERIFY(saveCopy(root+"/inputs/pack.res",publishedArchive));QVERIFY(saveCopy(root+"/maps/test.mpr",remapped.mprBytes));
        QJsonObject cfg{{"testRoot",root},{"archive",root+"/inputs/pack.res"},{"sources",QJsonArray{root+"/inputs/pack.res"}},
            {"maps",QJsonArray{root+"/maps/test.mpr"}},{"mode","build"},{"output",root+"/out"},{"receipt",root+"/receipt.json"}};
        QVERIFY(saveCopy(root+"/fixture.json",QJsonDocument(cfg).toJson()));
        QCOMPARE(runTerrainHdRepublish(root+"/fixture.json"),0);
        QFile repeatedMap(root+"/out/test/test.mpr");QVERIFY(repeatedMap.open(QIODevice::ReadOnly));QCOMPARE(repeatedMap.readAll(),remapped.mprBytes);
        QFile repeatedPack(root+"/out/test/HDPack_Terrain.res");QVERIFY(repeatedPack.open(QIODevice::ReadOnly));QCOMPARE(repeatedPack.readAll(),publishedArchive);
        cfg["receipt"]=dir.path()+"/outside.json";QVERIFY(saveCopy(root+"/fixture.json",QJsonDocument(cfg).toJson()));
        QCOMPARE(runTerrainHdRepublish(root+"/fixture.json"),2);QVERIFY(!QFileInfo::exists(dir.path()+"/outside.json"));

        // An SD frame keeps the complete animation native, never stretched.
        SMprAnimatedTileRecord interval;interval.tileIndex=2;interval.frameCount=3;
        const auto animation=rebuildTerrainAtlasHd(profile(partialDecoded),{partialSd},sources,true,{interval});
        QVERIFY(animation.success);QCOMPARE(animation.receipt["animationBlockedHd"].toInt(),2);
        for(int frame=2;frame<5;++frame)QCOMPARE(animation.after[animation.slotRemap[frame]].width(),128);
        QCOMPARE(animation.slotRemap[4],animation.slotRemap[2]+2);
        const auto conservative=rebuildTerrainAtlasHd(profile(partialDecoded),{partialSd},sources,false);QCOMPARE(conservative.pages[0],partialSd);
        // Different native HD tiles with an identical SD mip must be refused.
        auto collision=hd;const auto otherBase=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(QVector<QImage>(64,pattern(1)),0,256),true,nullptr);
        collision.replace(76,2048*2048/2,otherBase.mid(76,2048*2048/2));
        for(int row=0;row<64;++row){const int offset=76+((448+row)*512)*8;collision.replace(offset,64*8,hd.mid(offset,64*8));}
        SResArchiveNewMember alias;alias.name="other000.mmp";alias.payload=collision;
        CResArchiveDocument collisionArchive;QVERIFY(collisionArchive.loadBytes(empty));QByteArray collisionRes;QVERIFY(collisionArchive.buildWithAppendedMembers({member,alias},&collisionRes));
        QSaveFile second(dir.path()+"/collision.res");QVERIFY(second.open(QIODevice::WriteOnly));QVERIFY(second.write(collisionRes)==collisionRes.size());QVERIFY(second.commit());
        CTerrainHdSources ambiguous;QVERIFY(ambiguous.open({second.fileName()},&error));
        QVERIFY(!ambiguous.match(before[0],{},&wrong,&reason));QCOMPARE(reason,QString("ambiguous-HD-collision"));
        STerrainAtlasProfileSlot conflicting;conflicting.sourceNamespace="other";conflicting.sourceSlotAliases={0,2};
        QVERIFY(!ambiguous.match(before[0],conflicting,&wrong,&reason));QCOMPARE(reason,QString("ambiguous-provenance-HD-aliases"));
        STerrainTransitionSignature sig;sig.northWest=sig.northEast=1;sig.southWest=sig.southEast=2;
        QMap<qint32,QImage> lowSources{{1,before[0]},{2,before[1]}};
        QMap<qint32,QImage> highSources{{1,first.after[0]},{2,first.after[1]}};
        QMap<qint32,ETerrainBlendStyle> styles{{1,ETerrainBlendStyle::Grass},{2,ETerrainBlendStyle::Soil}};
        CTerrainTransitionCatalog noAuthored;
        CTerrainVirtualAtlas old(noAuthored,{before[0]},0,lowSources,styles,true);
        before[2]=old.reviewGeneratedBlend(sig,0,nullptr,nullptr,1);QVERIFY(!before[2].isNull());
        const auto generatedSd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(before,0,128),true,nullptr);
        QVector<QImage> decoded;for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(generatedSd,slot,&image));decoded.append(image);}
        p=profile(decoded);p.slotDefinitions[2].signature=CTerrainTransitionCatalog::mirroredGridY(sig);p.slotDefinitions[2].routeOwnerMaterial=1;
        const auto upgraded=rebuildTerrainAtlasHd(p,{generatedSd},sources);QVERIFY2(upgraded.success,qPrintable(upgraded.error));
        QCOMPARE(upgraded.receipt["regeneratedApplied"].toInt(),1);QCOMPARE(upgraded.after[2].size(),QSize(256,256));
        QCOMPARE(upgraded.profile.slotDefinitions[2].signature,p.slotDefinitions[2].signature);
        QCOMPARE(upgraded.profile.slotDefinitions[2].routeOwnerMaterial,qint32(1));
        CTerrainVirtualAtlas next(noAuthored,{first.after[0]},0,highSources,styles,true);
        const auto expected=next.reviewGeneratedBlend(sig,0,nullptr,nullptr,1);
        const auto encoded=ei::encodeMmpDxt1(expected,false,nullptr);QImage expectedDecoded;
        QVERIFY(ei::decodeDxtImage(encoded.mid(76),256,256,ei::DxtFormat::Dxt1,&expectedDecoded));QCOMPARE(upgraded.after[2],expectedDecoded);
        QMap<QPair<qint32,qint32>,STerrainRecipeBrush> bank;
        QVERIFY(CTerrainMixedEdge::buildStockBrushes(decoded,{{1,{0}},{2,{1}}},{{qMakePair(1,2),{2}}},{{2,sig}},{qMakePair(1,2)},&bank));
        STerrainRecipeBakeInput input;input.tileWidth=input.tileHeight=1;input.vertexMaterials={1,1,2,2};input.preferredRuntimeTypes={1};
        for(int id=1;id<=2;++id){STerrainRecipeMaterial material;material.materialId=id;material.runtimeTerrainType=id;material.uniformTiles={decoded[id-1]};input.materials.append(material);}
        input.brushes={bank.first()};STerrainRecipeBakeOptions options;options.tileSize=128;options.stableCoordinateVariants=true;options.preserveReviewedCellRuntimeTypes=true;
        const auto baked=CTerrainRecipeBaker::bake(input,options);QVERIFY2(baked.success,qPrintable(baked.diagnostics.join('\n')));
        decoded[3]=baked.tiles[baked.tileByCell.first()].pixels;
        const auto mixedSd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(decoded,0,128),true,nullptr);QVector<QImage> mixedDecoded;
        for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(mixedSd,i,&image));mixedDecoded.append(image);}
        auto mixedProfile=profile(mixedDecoded);mixedProfile.slotDefinitions[2]=p.slotDefinitions[2];mixedProfile.slotDefinitions[3].signature=p.slotDefinitions[2].signature;
        QByteArray recipeKey("crisp-stock-v1|1,1,2,2,|");recipeKey+=QByteArray::number(baked.recipes.first().variant)+"|1";
        mixedProfile.slotDefinitions[3].mixedEdgeRecipes=QStringList{"mixed-edge-b2:"+QString::fromLatin1(QCryptographicHash::hash(recipeKey,QCryptographicHash::Sha256).toHex())};
        const auto mixedHd=rebuildTerrainAtlasHd(mixedProfile,{mixedSd},sources);
        QVERIFY2(mixedHd.success,qPrintable(mixedHd.error));
        QVERIFY2(mixedHd.receipt["slots"].toArray()[3].toObject()["regenerated"].toBool(),qPrintable(QString(QJsonDocument(mixedHd.receipt).toJson())));
        QCOMPARE(mixedHd.after[3].size(),QSize(256,256));QCOMPARE(mixedHd.profile.slotDefinitions[3].mixedEdgeRecipes,mixedProfile.slotDefinitions[3].mixedEdgeRecipes);
    }
    void cleanupOrphanAndSharedNamespace()
    {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const QString ns="published_123456789abc",next="published_987654321abc";
        QDir().mkpath(dir.path()+"/Dejavu");QDir().mkpath(dir.path()+"/Base");
        const QString current=dir.path()+"/Dejavu/current.mpr",baseMap=dir.path()+"/Base/base.mpr";
        auto save=[](const QString& path,const QByteArray& bytes){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();};
        CMprDocument map;QVERIFY(map.loadBytes(mapBytes(),"fixture"));
        auto named=[&](const QString& name){QByteArray bytes;CMprEditTransaction tx;map.applyIsolatedNamespaceTransaction(tx,name,&bytes);return bytes;};
        QVERIFY(save(current,named(next)));QVERIFY(save(baseMap,mapBytes()));
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument blank;QVERIFY(blank.loadBytes(empty));
        auto p=profile(QVector<QImage>(64,pattern(0)));p.mprNamespace=ns;p.provenance=CTerrainAtlasProfileStore::workingAtlasProvenance();
        QVector<SResArchiveNewMember> members;
        for(const auto& pair:QVector<QPair<QString,QByteArray>>{{ns+"000.mmp","old-page"},{CTerrainAtlasProfileStore::embeddedMemberName(ns),p.toBytes()},{"stock000.mmp","stock"},{next+"000.mmp","new-page"},{ns+"000.mmp.extra","unrelated"}}) {
            SResArchiveNewMember m;m.name=pair.first;m.payload=pair.second;m.lastWriteTime=123;members.append(m);
        }
        QByteArray bytes;QVERIFY(blank.buildWithAppendedMembers(members,&bytes));CResArchiveDocument archive;QVERIFY(archive.loadBytes(bytes));
        const QStringList roots{dir.path()+"/Dejavu",dir.path()+"/Base"};QByteArray candidate;QJsonObject receipt;QString error;
        auto stockProfile=p;stockProfile.provenance="authored-template:stock";
        SResArchiveNewMember stockMetadata;stockMetadata.name=CTerrainAtlasProfileStore::embeddedMemberName(ns);stockMetadata.payload=stockProfile.toBytes();
        QByteArray stockBytes;QVERIFY(archive.buildWithPublishedMembers({stockMetadata},true,&stockBytes));
        CResArchiveDocument stockArchive;QVERIFY(stockArchive.loadBytes(stockBytes));
        QVERIFY(buildTerrainHdCleanup(stockArchive,ns,next,current,roots,&candidate,&receipt,&error));
        QCOMPARE(candidate,stockBytes);QCOMPARE(receipt["reason"].toString(),QString("stock-or-unverified-original-atlas"));

        QVERIFY(buildTerrainHdCleanup(archive,ns,next,current,roots,&candidate,&receipt,&error));
        QCOMPARE(receipt["removedMembers"].toInt(),2);CResArchiveDocument cleaned;QVERIFY(cleaned.loadBytes(candidate));
        QCOMPARE(cleaned.members().size(),3);QCOMPARE(cleaned.memberPayload("stock000.mmp"),QByteArray("stock"));
        QCOMPARE(cleaned.memberPayload(ns+"000.mmp.extra"),QByteArray("unrelated"));
        QVERIFY(save(baseMap,named(ns)));
        QVERIFY(buildTerrainHdCleanup(archive,ns,next,current,roots,&candidate,&receipt,&error));QCOMPARE(candidate,bytes);QCOMPARE(receipt["reason"].toString(),QString("shared-namespace"));
        QVERIFY(save(baseMap,QByteArray("broken")));
        QVERIFY(buildTerrainHdCleanup(archive,ns,next,current,roots,&candidate,&receipt,&error));QCOMPARE(candidate,bytes);QVERIFY(receipt["reason"].toString().startsWith("unreadable-MPR"));
        QVERIFY(buildTerrainHdCleanup(archive,"stock",next,current,roots,&candidate,&receipt,&error));QCOMPARE(candidate,bytes);QCOMPARE(receipt["reason"].toString(),QString("stock-or-unverified-original-atlas"));
        QVERIFY(buildTerrainHdCleanup(archive,ns,next,current,{roots.first()},&candidate,&receipt,&error));QCOMPARE(candidate,bytes);
        QVERIFY(save(baseMap,mapBytes()));
        QVERIFY(buildTerrainHdCleanup(archive,ns,next,current,roots,&candidate,&receipt,&error));
        const QString archivePath=dir.path()+"/pack.res";QVERIFY(save(archivePath,bytes));
        // An MPR appearing after the orphan scan cancels cleanup before any write.
        const QString lateMap=dir.path()+"/Base/late.mpr";QVERIFY(save(lateMap,named(ns)));
        QVERIFY(!commitTerrainHdCleanup(archivePath,bytes,candidate,receipt,&error));
        QFile readback(archivePath);QVERIFY(readback.open(QIODevice::ReadOnly));QCOMPARE(readback.readAll(),bytes);readback.close();
        QVERIFY(QFile::remove(lateMap));
        // Exact archive baseline is mandatory even if the candidate is valid.
        QVERIFY(!commitTerrainHdCleanup(archivePath,bytes+"changed",candidate,receipt,&error));
        QVERIFY2(commitTerrainHdCleanup(archivePath,bytes,candidate,receipt,&error),qPrintable(error));
        QVERIFY(readback.open(QIODevice::ReadOnly));QCOMPARE(readback.readAll(),candidate);

    }
    void expandedAtlasKeepsNativeSd()
    {
        QVector<QImage> tile(64,pattern(0).scaled(128,128));
        const auto page=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tile,0,128),true,nullptr);
        QVector<QImage> decoded;
        for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(page,i,&image));decoded.append(image);}
        QVector<QImage> images;for(int i=0;i<18;++i)images+=decoded;
        auto p=profile(decoded);p.textureCount=18;p.atlasSlotCount=1152;p.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(images);
        for(int i=64;i<1152;++i){auto d=p.slotDefinitions[i%64];d.slot=i;p.slotDefinitions.append(d);}
        CTerrainHdSources empty;const auto result=rebuildTerrainAtlasHd(p,QVector<QByteArray>(18,page),empty);
        QVERIFY2(result.success,qPrintable(result.error));QCOMPARE(result.after.size(),1152);QCOMPARE(result.pages,QVector<QByteArray>(18,page));
    }
    void missingSourceKeepsOriginalPage()
    {
        QVector<QImage> tiles(64,pattern(0).scaled(128,128));
        const auto sd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,128),true,nullptr);QVector<QImage> before;
        for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,slot,&image));before.append(image);}
        CTerrainHdSources empty;auto result=rebuildTerrainAtlasHd(profile(before),{sd},empty);
        QVERIFY(result.success);QCOMPARE(result.pages[0],sd);QCOMPARE(result.receipt["retained128"].toInt(),64);QCOMPARE(result.profile.atlasPixelSha256,CTerrainAtlasProfileInference::pixelSha256(before));
    }
};
int main(int argc,char** argv)
{
    ei::TestClipboardSession clipboard(true);if(!clipboard.prepared())return 2;
    QApplication app(argc,argv);if(!clipboard.install())return 2;
    TerrainHdRepublishTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "terrain_hd_republish_tests.moc"
