#include "terrain_height_res_save_check.h"
#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "tree_view.h"
#include "terrain_semantic_field_store.h"
#include "terrain_virtual_atlas.h"
#include "endurance_document.h"
#include "res_archive_document.h"
#include "mmp_codec.h"
#include "resource_name.h"
#include <QApplication>
#include <QAbstractButton>
#include <QCryptographicHash>
#include <QFile>
#include <QLineEdit>
#include <QJsonDocument>
#include <QMessageBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUndoStack>
#include <QtEndian>

int runTerrainHeightResSaveCheck()
{
    QTextStream out(stdout); int failed=0;
    auto check=[&](bool ok,const char* label) {
        out<<(ok?"PASS: ":"FAIL: ")<<label<<"\n";out.flush();if(!ok)++failed;return ok;
    };
    auto hash=[](const QByteArray& b){return QCryptographicHash::hash(b,QCryptographicHash::Sha256);};
    auto read=[](const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();};
    auto write=[](const QString& path,const QByteArray& b){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size();};
    QTemporaryDir temp; if(!temp.isValid())return 2;
    CValidationReport report; CMprDocument source;
    if(!source.load(QFileInfo(qEnvironmentVariable("EI_TEST_TERRAIN_FIXTURE")),&report))return 2;
    SMprCreationRequest creation;creation.innerNamespace="height122";
    creation.flatRawHeight=10000;creation.maxZ=100;
    QByteArray original;if(!source.buildNewCanvas(creation,&original,&report))return 2;
    CMprDocument mpr;CTerrainEditModel terrain;
    if(!mpr.loadBytes(original,"height122.mpr",&report)||!terrain.load(mpr,&report))return 2;
    const int fieldCount=(terrain.tileCountX()+1)*(terrain.tileCountY()+1);
    const QVector<qint32> field(fieldCount,1);
    terrain.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,field,field,&report);
    QImage tile(128,128,QImage::Format_RGBA8888);tile.fill(Qt::green);
    QVector<QImage> images(int(mpr.mapInfo().textureCount)*64,tile);
    STerrainAtlasProfile profile;profile.mprNamespace=mpr.mapInfo().innerNamespace;
    profile.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(images);
    profile.atlasSlotCount=images.size();profile.textureCount=int(mpr.mapInfo().textureCount);
    profile.provenance=CTerrainAtlasProfileStore::workingAtlasProvenance();
    STerrainAtlasProfileFamily family;family.id=1;family.name="Grass";
    for(int i=0;i<images.size();++i) {
        family.memberSlots.append(i);STerrainAtlasProfileSlot slot;slot.slot=i;
        slot.signature.northWest=slot.signature.northEast=slot.signature.southEast=slot.signature.southWest=1;
        profile.slotDefinitions.append(slot);
    }
    profile.families.append(family);
    STerrainSemanticFieldDocument legacy;legacy.schemaVersion=1;
    legacy.mprNamespace=profile.mprNamespace;legacy.mprSha256=hash(original);
    legacy.atlasPixelSha256=QByteArray::fromHex(profile.atlasPixelSha256);
    legacy.profileSha256=hash(profile.toBytes());legacy.vertexWidth=terrain.tileCountX()+1;
    legacy.vertexHeight=terrain.tileCountY()+1;legacy.land=field;
    const QString path=temp.filePath("height122.mpr");
    const QString archivePath=temp.filePath("HDPack_Terrain.res");
    QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));
    qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
    CResArchiveDocument emptyArchive;QStringList errors;
    if(!emptyArchive.loadBytes(empty,"synthetic.res",&errors))return 2;
    QVector<SResArchiveNewMember> members;
    SResArchiveNewMember sem;sem.name=STerrainSemanticFieldDocument::memberName(profile.mprNamespace,QFileInfo(path));
    sem.payload=legacy.toBytes(&report);members.append(sem);
    SResArchiveNewMember prof;prof.name=CTerrainAtlasProfileStore::embeddedMemberName(profile.mprNamespace);
    prof.payload=profile.toBytes();members.append(prof);
    QString encodeError;const auto mmp=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(images,0,128),true,&encodeError);
    for(const auto& name:ei::terrainTextureResourceNames(profile.mprNamespace,profile.textureCount)) {
        SResArchiveNewMember page;page.name=name;page.payload=mmp;members.append(page);
    }
    QByteArray archiveBytes;
    if(!emptyArchive.buildWithAppendedMembers(members,&archiveBytes,&errors)
       ||!write(path,original)||!write(archivePath,archiveBytes))return 2;
    const auto archiveTime=QFileInfo(archivePath).lastModified();
    CSettings settings;QTableWidget properties;properties.setColumnCount(2);
    QUndoStack history;CProgressView progress;QLineEdit coordinates;CTreeView tree;CTileForm tiles;
    CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
    view.resize(320,240);view.show();QApplication::processEvents();view.makeCurrent();
    view.m_pLand->readMap(QFileInfo(path),false);view.m_mprDocument=mpr;
    view.m_terrainEdit=terrain;view.m_terrainEditAvailable=true;
    view.m_pLand->setCanonicalTerrainModel(&view.m_terrainEdit);
    view.m_mprMaterials=mpr.mapInfo().materials;view.m_mprAnimatedTiles=mpr.mapInfo().animatedTiles;
    view.m_terrainAtlasProfile=profile;view.m_activeTerrainArchivePath=archivePath;
    int publications=0,unexpectedDialogs=0;
    QObject::connect(&view,&CView::terrainPublicationPreflightRequested,[&](const QString&,bool* ready){++publications;*ready=true;});
    QTimer modalTimer;modalTimer.setInterval(20);
    QObject::connect(&modalTimer,&QTimer::timeout,[&](){for(auto* widget:QApplication::topLevelWidgets()) {
        auto* box=qobject_cast<QMessageBox*>(widget);if(!box||!box->isVisible())continue;
        if(box->standardButtons()&QMessageBox::Save)box->button(QMessageBox::Save)->click();
        else {++unexpectedDialogs;out<<"DIALOG: "<<box->text()<<"\n";box->button(QMessageBox::Ok)->click();}
    }});modalTimer.start();
    CTerrainEditModel restored=terrain;
    check(view.restoreTerrainSemanticFieldFromArchive(mpr,profile,&restored,QFileInfo(path),archivePath,true),
          "legacy v1 RES material field restores before editing");
    STerrainBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=1;
    auto height=view.m_terrainEdit.makeRaiseLower(brush,100);
    view.m_terrainEdit.apply(height,true,&report);view.m_pLand->setDirty(true);
    check(view.currentTerrainMprSaveRoute(false)==ETerrainMprSaveRoute::PlainMpr,
          "height-only working profile chooses ordinary MPR save");
    check(CTerrainAtlasProfileStore::isWorkingAtlasProfile(view.m_terrainAtlasProfile),
          "production saved-working-profile regression uses the real provenance marker");
    check(view.saveChangedLandscapeDocument(QFileInfo(path),false),"production local height-only save succeeds");
    const auto saved=read(path);
    check(saved!=original&&read(archivePath)==archiveBytes&&QFileInfo(archivePath).lastModified()==archiveTime
          &&publications==0,"local height save leaves RES bytes timestamp and publication count unchanged");
    CMprDocument reopened;CTerrainEditModel reopenedTerrain;STerrainSemanticFieldDocument embedded;
    check(reopened.loadBytes(saved,path,&report)&&reopenedTerrain.load(reopened,&report)
          &&STerrainSemanticFieldDocument::fromBytes(reopened.memberPayload(embedded.embeddedMemberName()),&embedded,&report)
          &&embedded.schemaVersion==2,"saved MPR embeds v2 material field");
    check(view.restoreTerrainSemanticFieldFromArchive(reopened,profile,&reopenedTerrain,QFileInfo(path),archivePath,true),
          "fresh reopen prefers embedded materials over obsolete v1 RES hash");
    QVector<qint32> current,baseline;reopenedTerrain.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&current,&baseline);
    check(current==field&&baseline==field,"fresh reopen restores exact corner materials");
    CResArchiveDocument formattedSource;formattedSource.loadBytes(archiveBytes,archivePath,&errors);
    SResArchiveNewMember formattedProfile=prof;
    formattedProfile.payload=QJsonDocument::fromJson(prof.payload).toJson(QJsonDocument::Indented);
    QByteArray formattedArchive;const QString formattedPath=temp.filePath("formatted-profile.res");
    check(formattedSource.buildWithPublishedMembers({formattedProfile},true,&formattedArchive,&errors)
          &&write(formattedPath,formattedArchive)
          &&view.restoreTerrainSemanticFieldFromArchive(reopened,profile,&reopenedTerrain,QFileInfo(path),formattedPath,true),
          "v2 profile binding ignores JSON whitespace while preserving material content");
    auto wrongProfile=profile;wrongProfile.families[0].name="Dirt";
    formattedProfile.payload=wrongProfile.toBytes();QByteArray wrongArchive;
    check(formattedSource.buildWithPublishedMembers({formattedProfile},true,&wrongArchive,&errors)
          &&write(formattedPath,wrongArchive)
          &&!view.restoreTerrainSemanticFieldFromArchive(reopened,profile,&reopenedTerrain,QFileInfo(path),formattedPath,true),
          "v2 rejects changed RES profile content even with a retained in-memory profile");
    check(embedded.tileBindingSha256(mpr)==embedded.tileBindingSha256(reopened),"height does not change material binding");
    check(read(archivePath)==archiveBytes,"legacy RES semantic bytes preserved after reopen");
    view.m_mprDocument=reopened;view.m_terrainEdit=reopenedTerrain;
    height=view.m_terrainEdit.makeRaiseLower(brush,-60);view.m_terrainEdit.apply(height,true,&report);
    check(view.saveChangedLandscapeDocument(QFileInfo(path),false)&&read(archivePath)==archiveBytes,
          "second height save reuses embedded semantic payload without touching RES");
    const QString copyPath=temp.filePath("height-copy.mpr");
    check(view.saveChangedLandscapeDocument(QFileInfo(copyPath),false)==false,
          "stale source baseline rejects a subsequent save without adoption");
    view.m_mprDocument.load(QFileInfo(path),&report);view.m_terrainEdit.load(view.m_mprDocument,&report);
    view.restoreTerrainSemanticFieldFromArchive(view.m_mprDocument,profile,&view.m_terrainEdit,QFileInfo(path),archivePath,true);
    height=view.m_terrainEdit.makeRaiseLower(brush,30);view.m_terrainEdit.apply(height,true,&report);
    check(view.saveChangedLandscapeDocument(QFileInfo(copyPath),false)&&read(archivePath)==archiveBytes,
          "changed MPR copy preserves embedded materials and RES");
    CMprDocument copy;CTerrainEditModel copyTerrain;
    check(copy.load(QFileInfo(copyPath),&report)&&copyTerrain.load(copy,&report)
          &&view.restoreTerrainSemanticFieldFromArchive(copy,profile,&copyTerrain,QFileInfo(copyPath),archivePath,true),
          "Save As different filename restores embedded material binding");
    // Two retained neighboring maps with atlases but only geometry edits.
    view.m_worldTerrainSession.clear();
    CTerrainTransitionCatalog catalog;STerrainTransitionVariant variant;
    variant.signature.northWest=variant.signature.northEast=variant.signature.southEast=variant.signature.southWest=1;
    variant.packedTile=0;
    catalog.addVariant(variant,&report);catalog.declareMaterial(1);
    for(const auto& filename:{QStringLiteral("world-a.mpr"),QStringLiteral("world-b.mpr")}) {
        const QString worldPath=temp.filePath(filename);write(worldPath,saved);
        view.m_worldTerrainSession.load(worldPath,&encodeError);
        const QString id=CWorldTerrainSession::key(worldPath);
        const auto doc=view.m_worldTerrainSession.documents[id];
        doc->atlas.reset(new CTerrainWorkingAtlas(catalog,images,0,kMprPackedTerrainAtlasSlotLimit,{}, {},mpr.mapInfo().tileTypes));
        doc->materials=mpr.mapInfo().materials;
        doc->terrain.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,field,field,&report);
        const auto worldHeight=doc->terrain.makeRaiseLower(brush,110);doc->terrain.apply(worldHeight,true,&report);
        SWorldTerrainAtlasSource atlasSource;atlasSource.archive=archivePath;atlasSource.profile=profile;
        for(int i=0;i<profile.textureCount;++i)atlasSource.pages.append(mmp);
        view.m_worldTerrainAtlasSources[id]=atlasSource;
    }
    check(view.saveWorldTerrainMaps(),"production two-map height-only world save succeeds");
    check(read(archivePath)==archiveBytes&&QFileInfo(archivePath).lastModified()==archiveTime&&publications==0,
          "world save with loaded atlases leaves shared RES untouched");
    for(const auto& filename:{QStringLiteral("world-a.mpr"),QStringLiteral("world-b.mpr")}) {
        CMprDocument world;CTerrainEditModel model;const QString worldPath=temp.filePath(filename);
        check(world.load(QFileInfo(worldPath),&report)&&world.originalBytes()!=saved&&model.load(world,&report)
              &&view.restoreTerrainSemanticFieldFromArchive(world,profile,&model,QFileInfo(worldPath),archivePath,true),
              "world map reopens with exact embedded semantic binding");
    }
    // A genuinely new referenced page still uses the production RES publisher.
    view.m_worldTerrainSession.clear();view.m_worldTerrainAtlasSources.clear();
    STerrainGlobalMaterialFamily globalFamily;globalFamily.id=1;globalFamily.name="Grass";
    globalFamily.layer=ETerrainAtlasLayer::Land;globalFamily.runtimeTerrainType=0;
    view.m_terrainGlobalFamilies.append(globalFamily);
    creation.innerNamespace="height-painted122";QByteArray paintOriginal;
    if(!source.buildNewCanvas(creation,&paintOriginal,&report))return 2;
    CMprDocument paintMpr;paintMpr.loadBytes(paintOriginal,"world-painted.mpr",&report);
    const QString paintPath=temp.filePath("world-painted.mpr");
    const QString paintArchivePath=temp.filePath("world-painted-atlas.res");
    STerrainAtlasProfile paintProfile=profile;paintProfile.mprNamespace=creation.innerNamespace;
    QVector<SResArchiveNewMember> paintMembers;
    prof.name=CTerrainAtlasProfileStore::embeddedMemberName(paintProfile.mprNamespace);
    prof.payload=paintProfile.toBytes();paintMembers.append(prof);
    for(const auto& name:ei::terrainTextureResourceNames(paintProfile.mprNamespace,paintProfile.textureCount)) {
        SResArchiveNewMember page;page.name=name;page.payload=mmp;paintMembers.append(page);
    }
    QByteArray paintArchive;
    if(!emptyArchive.buildWithAppendedMembers(paintMembers,&paintArchive,&errors)
       ||!write(paintPath,paintOriginal)||!write(paintArchivePath,paintArchive)
       ||!view.m_worldTerrainSession.load(paintPath,&encodeError))return 2;
    const QString paintId=CWorldTerrainSession::key(paintPath);
    const auto paintDoc=view.m_worldTerrainSession.documents[paintId];
    paintDoc->atlas.reset(new CTerrainWorkingAtlas(catalog,images,0,kMprPackedTerrainAtlasSlotLimit,{}, {},paintMpr.mapInfo().tileTypes));
    paintDoc->terrain.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,field,field,&report);
    QImage newTile=tile;newTile.fill(Qt::red);
    const int newSlot=paintDoc->atlas->importPhysicalTile(newTile,0,&variant.signature,"foreign",0,&report,true);
    check(newSlot>=images.size(),"new foreign artwork occupies an unsaved atlas extension");
    paintDoc->terrain.enableWorkingAtlasSlots(kMprPackedTerrainAtlasSlotLimit);
    paintDoc->terrain.installWorkingAtlasSlotTypes(paintDoc->atlas->activeTileTypes());
    STerrainTileBrush paintBrush;paintBrush.centerX=8;paintBrush.centerY=8;paintBrush.radius=0;
    const auto paint=paintDoc->terrain.makeTilePaint(paintBrush,ETerrainTileLayer::Land,newSlot/64,newSlot%64,0);
    check(!paint.isEmpty()&&paintDoc->terrain.apply(paint,true,&report)
          &&paintDoc->terrain.usesUnsavedAtlasArtwork(*paintDoc->atlas),"referenced new tile requires RES publication");
    check(paintDoc->terrain.apply(paint,false,&report)
          &&!paintDoc->terrain.usesUnsavedAtlasArtwork(*paintDoc->atlas),"Undo ignores unused cached new artwork");
    check(paintDoc->terrain.apply(paint,true,&report)
          &&paintDoc->terrain.usesUnsavedAtlasArtwork(*paintDoc->atlas),"Redo restores the publication requirement");
    const auto paintHeight=paintDoc->terrain.makeRaiseLower(brush,50);paintDoc->terrain.apply(paintHeight,true,&report);
    SWorldTerrainAtlasSource paintSource;paintSource.archive=paintArchivePath;paintSource.profile=paintProfile;
    for(int i=0;i<paintProfile.textureCount;++i)paintSource.pages.append(mmp);
    view.m_worldTerrainAtlasSources[paintId]=paintSource;
    check(view.saveWorldTerrainMaps(),"production mixed painting and height world save succeeds");
    const QByteArray publishedArchive=read(paintArchivePath);
    check(publishedArchive!=paintArchive&&publications==1,"mixed save publishes the required new texture pages once");
    const auto publishedBinding=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({paintArchivePath},paintProfile.mprNamespace);
    CMprDocument paintedReopen;CTerrainEditModel paintedModel;
    check(publishedBinding.success()&&paintedReopen.load(QFileInfo(paintPath),&report)
          &&paintedModel.load(paintedReopen,&report)
          &&view.restoreTerrainSemanticFieldFromArchive(paintedReopen,publishedBinding.profile,&paintedModel,
              QFileInfo(paintPath),paintArchivePath,true)
          &&SMprPackedTile::decode(paintedModel.packedTile(8,8,ETerrainTileLayer::Land)).atlasSlot()==newSlot,
          "published new texture reference and exact materials survive reopen");
    const auto publishedTime=QFileInfo(paintArchivePath).lastModified();
    const auto afterPublishHeight=paintDoc->terrain.makeRaiseLower(brush,20);paintDoc->terrain.apply(afterPublishHeight,true,&report);
    check(view.saveWorldTerrainMaps()&&read(paintArchivePath)==publishedArchive
          &&QFileInfo(paintArchivePath).lastModified()==publishedTime&&publications==1,
          "height-only save after painting does not republish the saved working atlas");
    check(unexpectedDialogs==1,"only intentional stale-baseline failure produced a diagnostic dialog");
    view.m_worldTerrainSession.clear();view.m_pLand->setCanonicalTerrainModel(nullptr);view.m_pLand->unloadMpr();
    out<<"Terrain height RES save failures="<<failed<<"\n";out.flush();return failed?1:0;
}
