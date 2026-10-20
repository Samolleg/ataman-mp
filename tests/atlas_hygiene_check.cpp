// EI-ATLAS-HYGIENE-209: production-object checks; every writable path is owned.
#include <QtWidgets>
#include <QtOpenGL>
#include <QtTest>
#define private public
#include "view.h"
#undef private
#include "settings.h"
#include "resourcemanager.h"
#include "progressview.h"
#include "tree_view.h"
#include "tile_form.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "terrain_base_hd.h"
#include "terrain_base_hd_seams.h"
#include "terrain_base_hd_native.h"
#include "terrain_atlas_compact.h"
#include "terrain_atlas_archive_binding.h"
#include "terrain_semantic_field_store.h"
#include "endurance_document.h"
#include "mmp_codec.h"
#include "res_archive_index.h"
#include "staged_save_validator.h"
#include <QtEndian>
namespace {
QTextStream out(stdout);int checks=0;
void check(bool ok,const QString& name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw name;++checks;}
QByteArray read(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
void write(const QString& p,const QByteArray& b){QFile f(p);check(f.open(QIODevice::WriteOnly)&&f.write(b)==b.size(),"write owned receipt "+QFileInfo(p).fileName());}
QByteArray hash(const QByteArray& b){return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex();}
QString pageName(const QString& ns,int p){return QString("%1%2.mmp").arg(ns).arg(p,3,10,QLatin1Char('0'));}
void pack(const QString& root){
    const auto path=qEnvironmentVariableIsSet("EI_PACK_PREVIEW_SOURCE")?qEnvironmentVariable("EI_PACK_PREVIEW_SOURCE"):root+"/current/res/HDPack_Terrain.res";const QStringList dirs={root+"/current/Maps",root+"/current/BaseMaps"};
    auto plan=prepareTerrainPackCleanup(path,dirs);check(plan.error.isEmpty(),"cleanup complete inventory: "+plan.error);
    check(plan.inventory.size()==80,"all42 Dejavu and38 Base inventoried");check(!plan.members.isEmpty()&&plan.after<plan.before,"cleanup finds obsolete publications");
    QJsonArray rows;QString table="Namespace\tPages\tMiB\tDisposition\n";
    for(const auto& row:plan.rows){QJsonObject j;j["namespace"]=row.name;j["pages"]=row.pages;j["bytes"]=double(row.bytes);j["remove"]=row.removable;j["usage"]=row.usage;rows<<j;
        table+=QString("%1\t%2\t%3\t%4\n").arg(row.name).arg(row.pages).arg(row.bytes/1048576.0,0,'f',3).arg(row.usage);
        if(row.removable)check(terrainAtlasHasHash(row.name),"only hash namespace removed: "+row.name);}
    QJsonObject receipt;receipt["before"]=double(plan.before);receipt["after"]=double(plan.after);receipt["rows"]=rows;
    write(root+"/cleanup-table.tsv",table.toUtf8());write(root+"/cleanup-plan.json",QJsonDocument(receipt).toJson());
    if(qEnvironmentVariableIsSet("EI_PACK_PREVIEW_SOURCE")){out<<"PASS review preview only; no publication\n";out.flush();return;}
    // Preview and rejection of stale inventory perform no publication.
    auto invalid=plan;invalid.inventory.insert(root+"/missing.mpr","wrong");QString error;
    check(!commitTerrainPackCleanup(path,dirs,invalid,&error,root+"/backups")&&hash(read(path))==plan.archiveSha256,"changed inventory refuses cleanup without writing");
    check(commitTerrainPackCleanup(path,dirs,plan,&error,root+"/backups"),"confirmed cleanup safe publication: "+error);
    check(QFileInfo(path).size()==plan.after,"exact pack size matches preview");
    CResArchiveIndex archive;check(archive.open(path,&error),"reopen cleaned RES");int maps=0,pages=0;
    for(const auto& file:QDir(dirs[0]).entryInfoList({"*.mpr"},QDir::Files)){
        CMprDocument doc;CTerrainEditModel terrain;CValidationReport report;
        check(doc.load(file,&report)&&!report.hasErrors()&&terrain.load(doc,&report),"open Dejavu "+file.fileName());
        for(int i=0;i<int(doc.mapInfo().textureCount);++i){auto bytes=archive.readMember(pageName(doc.mapInfo().innerNamespace,i),&error);QImage image;
            if(!ei::decodeMmpDxt1AtlasTile(bytes,0,&image,&error))throw file.fileName()+" missing/corrupt page: "+error;++pages;}
        ++maps;
    }
    check(maps==42,"all42 Dejavu atlases resolve with zero missing pages");
    receipt["openedDejavuMaps"]=maps;receipt["resolvedPages"]=pages;write(root+"/cleanup-result.json",QJsonDocument(receipt).toJson());
}
void map(const QString& root){
    const auto fixture=root+"/historical/PZ/MODS/Dejavu";const auto path=fixture+"/Maps/zone2.mpr";
    QTemporaryDir settingsRoot(root+"/settings-XXXXXX");check(settingsRoot.isValid(),"isolated settings");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsRoot.path());
    auto* settings=new CSettings;dynamic_cast<COptString*>(settings->opt("resourceProfile"))->setValue("deja-vu");
    dynamic_cast<COptString*>(settings->opt("gameRoot"))->setValue(root+"/historical/PZ");
    dynamic_cast<COptStringList*>(settings->opt(eOptSetResource,"resourceRoots"))->setValue({fixture+"/res",root+"/historical/PZ/Base/Res"});
    CTextureList::getInstance()->attachSettings(settings);
    auto* history=new QUndoStack;auto* table=new QTableWidget;table->setColumnCount(2);auto* view=new CView(nullptr,nullptr);auto* picker=new CTileForm;
    view->setAttribute(Qt::WA_DontShowOnScreen);view->attach(settings,table,history,new CProgressView,new QLineEdit,new CTreeView,picker);
    view->resize(640,480);view->show();QCoreApplication::processEvents();check(view->isValid(),"hidden OpenGL context");
    int unexpectedDialogs=0;QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,view,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* b=qobject_cast<QMessageBox*>(w)){
        out<<"DIALOG "<<b->windowTitle()<<": "<<b->text()<<"\n";out.flush();++unexpectedDialogs;b->reject();}});dialogs.start(100);
    view->makeCurrent();CTextureList::getInstance()->initResource();auto* panel=new CLandscapeTools;view->attachLandscapeTools(panel);
    view->loadLandscape(QFileInfo(path),false);view->makeCurrent();check(view->ensureTerrainAutoCatalogReady(),"open real pre-HD zone2 and palette catalog");
    if(qEnvironmentVariable("EI_ATLAS_HYGIENE_MODE")=="guards"){
        const auto before=read(path);const auto source=view->m_mprDocument;
        check(validateStagedSave(before,QFileInfo(path)).success,"common final-save validator accepts valid compact map");
        view->m_workingTerrainAtlas.clear();
        check(!view->saveLandscapeDocument(QFileInfo(path))&&read(path)==before,"missing working atlas refuses raw Save fallback without writing");
        check(unexpectedDialogs==1,"missing atlas gives one actionable refusal");
        auto mp=source.memberPayload(source.mapInfo().memberName);qToLittleEndian<quint32>(127,reinterpret_cast<uchar*>(mp.data()+16));
        CMprEditTransaction tx("oversized save guard");tx.replaceMemberPayload(source.mapInfo().memberName,hash(source.memberPayload(source.mapInfo().memberName)),mp);
        QByteArray oversized;CValidationReport report;check(source.applyTransaction(tx,&oversized,&report),"oversized source candidate represented");
        const auto limit=validateStagedSave(oversized,QFileInfo(path));
        check(!limit.success&&limit.error.contains("127")&&limit.error.contains("126"),"common final-save validator blocks127 pages");
        const auto preview=validateStagedMprTransaction(source,tx,QFileInfo(path));
        check(preview.success,"intermediate oversized candidate remains available for compaction");
        SMprSaveCommitRequest request;request.source=QFileInfo(path);request.destination=QFileInfo(path);request.backupRootOverride=root+"/backups";
        auto final=commitStagedMprTransaction(source,tx,request);
        check(!final.success&&final.phase=="atlas-limit"&&read(path)==before,"transaction save blocks127 before backup/write");return;
    }
    check(view->m_mprDocument.mapInfo().textureCount==16,"pre-HD fixture has16 pages");
    check(view->m_workingTerrainAtlas&&view->m_workingTerrainAtlas->isValid()&&picker->tileImages().size()>0,"owner palette nonempty");
    const auto originalTerrain=view->m_terrainEdit;const auto originalImages=view->m_workingTerrainAtlas->activeTileImages();const auto originalDoc=view->m_mprDocument;
    QVector<QByteArray> pages;QString error;CResArchiveIndex archive;check(archive.open(fixture+"/res/HDPack_Terrain.res",&error),"historical pack copy");
    for(int i=0;i<16;++i)pages<<archive.readMember(pageName("zone2",i),&error);
    // Canonical BC1 rotations must match QImage rotations, including selectors.
    auto blocks=baseHdNativeBlocks(pages[0],0);QImage decoded;ei::decodeDxtImage(blocks,256,256,ei::DxtFormat::Dxt1,&decoded,&error);
    for(int t=0;t<4;++t){QImage rotated;ei::decodeDxtImage(baseHdRotateBlocks(blocks,t),256,256,ei::DxtFormat::Dxt1,&rotated,&error);
        check(rotated==decoded.transformed(QTransform().rotate(t*90),Qt::FastTransformation),"BC1 rotation exact "+QString::number(t));}
    QString summary;check(view->convertBaseTerrainToHd(qEnvironmentVariable("EI_HD_LIBRARY"),209,&summary,false),"first HD conversion: "+summary);
    const auto hdTerrain=view->m_terrainEdit;const auto hdImages=view->m_workingTerrainAtlas->activeTileImages();
    CBaseHdSeams seams(originalImages,hdImages);int changed=0;QSet<int> outputs;QPoint example(-1,-1);
    for(int y=0;y<hdTerrain.tileCountY();++y)for(int x=0;x<hdTerrain.tileCountX();++x){bool ok=false;const auto old=originalTerrain.packedTile(x,y,ETerrainTileLayer::Land,&ok);if(!ok)continue;
        const auto now=hdTerrain.packedTile(x,y,ETerrainTileLayer::Land);if(old==now)continue;++changed;outputs.insert(SMprPackedTile::decode(now).atlasSlot());if(example.x()<0)example={x,y};
        for(int side=0;side<4;++side)if(!seams.boundarySafe(old,now,side))throw QString("changed visible boundary");
        if(originalTerrain.packedTile(x,y,ETerrainTileLayer::Overlay)!=hdTerrain.packedTile(x,y,ETerrainTileLayer::Overlay))throw QString("overlay changed");}
    check(changed>0&&outputs.size()>1,"HD accepted cells and variants; all visible boundaries/overlays unchanged");
    int imports=0;for(int slot=0;slot<view->m_workingTerrainAtlas->activeTileCount();++slot){STerrainWorkingTileIdentity identity;
        if(view->m_workingTerrainAtlas->tileIdentityForSlot(slot,&identity)&&identity.sourceNamespace.startsWith("base-hd")){++imports;if(!outputs.contains(slot))throw QString("unreferenced HD import");}}
    check(imports==outputs.size(),"no imports from rejected/unselected cells");
    QImage comparison(12*160,6*160,QImage::Format_RGB32);comparison.fill(Qt::black);QPainter painter(&comparison);
    for(int side=0;side<2;++side)for(int y=0;y<6;++y)for(int x=0;x<6;++x){int cx=qBound(0,example.x()+x-2,hdTerrain.tileCountX()-1),cy=qBound(0,example.y()+y-2,hdTerrain.tileCountY()-1);
        auto p=SMprPackedTile::decode((side?hdTerrain:originalTerrain).packedTile(cx,cy,ETerrainTileLayer::Land));QImage image=(side?hdImages:originalImages)[p.atlasSlot()];image=image.transformed(QTransform().rotate(p.rotation*90));
        painter.drawImage(QRect((side*6+x)*160,y*160,160,160),image.copy(image.width()/16,image.height()/16,image.width()*7/8,image.height()*7/8));}
    painter.end();comparison.save(root+"/zone2-hd-before-after.png");
    check(view->saveChangedLandscapeDocument(QFileInfo(path),true,true),"save auto-compacts and publishes hash namespace");
    check(history->count()==0,"compaction clears Undo commands with previous slot numbers");
    auto firstDoc=view->m_mprDocument;int firstPages=firstDoc.mapInfo().textureCount;check(terrainAtlasHasHash(firstDoc.mapInfo().innerNamespace),"bare zone2 migrated to own hash namespace");
    auto first=view->m_workingTerrainAtlas->stateFingerprint();QString repeated;
    bool second=view->convertBaseTerrainToHd(qEnvironmentVariable("EI_HD_LIBRARY"),210,&repeated,false);
    check(!second&&view->m_workingTerrainAtlas->stateFingerprint()==first,"second HD run no changes, no growth: "+repeated);
    check(view->m_mprDocument.mapInfo().textureCount==quint32(firstPages),"second run same saved page count");
    // Owner scenario: a real palette tile stamp, Save, fresh reopen.
    bool found=false;quint16 replacement=0;
    for(int i=0;i<view->m_workingTerrainAtlas->activeTileCount();++i){SMprPackedTile p;p.atlasIndex=i/64;p.tileIndex=i%64;bool known=false;
        auto sig=view->m_workingTerrainAtlas->catalog().gridSignatureForPacked(p.encode(),&known);
        if(known&&sig.northWest&&sig.northWest==sig.northEast&&sig.northWest==sig.southWest&&sig.northWest==sig.southEast
           &&p.encode()!=view->m_terrainEdit.packedTile(example.x(),example.y(),ETerrainTileLayer::Land)){replacement=p.encode();found=true;break;}}
    check(found,"palette has a different paintable source");STerrainTileBrush brush;brush.centerX=example.x();brush.centerY=example.y();brush.radius=0;brush.shape=ETerrainTileBrushShape::Square;
    const auto tile=SMprPackedTile::decode(replacement);CValidationReport report;
    auto stamp=view->m_terrainEdit.makeTilePaint(brush,ETerrainTileLayer::Land,tile.atlasIndex,tile.tileIndex,tile.rotation,view->m_workingTerrainAtlas->catalog(),&report);
    check(!stamp.isEmpty()&&view->applyTerrainTilePreview(stamp,true),"owner paint stamp changes real cell");
    check(view->saveChangedLandscapeDocument(QFileInfo(path),true,true),"owner Save succeeds");
    CResArchiveIndex savedArchive;check(savedArchive.open(fixture+"/res/HDPack_Terrain.res",&error),"reopen after owner Save");
    check(savedArchive.readMember(pageName(firstDoc.mapInfo().innerNamespace,0),&error).isEmpty(),"previous hash namespace removed after successful new pair");
    check(!savedArchive.readMember(pageName("zone2",0),&error).isEmpty(),"original bare namespace preserved");
    auto final=view->m_mprDocument;auto beforeReopen=view->m_terrainEdit.packedTile(example.x(),example.y(),ETerrainTileLayer::Land);
    view->unloadLand(false);view->loadLandscape(QFileInfo(path),false);check(view->ensureTerrainAutoCatalogReady()&&view->m_workingTerrainAtlas->isValid()&&!picker->tileImages().isEmpty(),"owner reopen: palette and atlas available");
    check(view->m_terrainEdit.packedTile(example.x(),example.y(),ETerrainTileLayer::Land)==beforeReopen,"owner stroke persists after fresh reopen");
    // Synthetic126/127 boundary, including actual disk coordinator refusal.
    check(terrainAtlasPageLimit(126,&error)&&!terrainAtlasPageLimit(127,&error)&&error.contains("127")&&error.contains("126"),"126 allowed,127 forbidden with actionable numbers");
    CMprEditTransaction tx("synthetic127");auto mp=final.memberPayload(final.mapInfo().memberName);qToLittleEndian<quint32>(127,reinterpret_cast<uchar*>(mp.data()+16));
    tx.replaceMemberPayload(final.mapInfo().memberName,hash(final.memberPayload(final.mapInfo().memberName)),mp);QByteArray synthetic;
    check(final.applyTransaction(tx,&synthetic,&report),"synthetic127-page MPR generated");
    SMprBytesSaveCommitRequest request;request.source=QFileInfo(path);request.destination=QFileInfo(path);request.expectedSourceBytes=read(path);request.candidateBytes=synthetic;request.backupRootOverride=root+"/backups";
    auto blocked=commitValidatedMprBytes(request);check(!blocked.success&&blocked.phase=="atlas-limit"&&read(path)==request.expectedSourceBytes,"actual save blocks127 without replacing destination");
    check(unexpectedDialogs==0,"no unexpected modal errors during owner scenario");
    QJsonObject receipt;receipt["beforePages"]=16;receipt["firstPages"]=firstPages;receipt["secondPages"]=firstPages;receipt["changedCells"]=changed;receipt["importedVariants"]=imports;receipt["finalNamespace"]=final.mapInfo().innerNamespace;receipt["ownerScenario"]=true;
    write(root+"/map-result.json",QJsonDocument(receipt).toJson());out<<"RESULT zone2 16 -> "<<firstPages<<" -> "<<firstPages<<"\n";out.flush();
}
}
int runScenarioLayerCheck(){try{out.setCodec("UTF-8");const auto root=qEnvironmentVariable("EI_ATLAS_HYGIENE_ROOT");if(!QFileInfo(root).fileName().startsWith("build-tests-atlas-hygiene-209"))return 2;
    if(qEnvironmentVariable("EI_ATLAS_HYGIENE_MODE")=="pack")pack(root);else map(root);
    out<<"PASS hygiene "<<checks<<" checks nativeAttempts=0\n";out.flush();return 0;
}catch(const QString& e){out<<"FAIL "<<e<<"\n";out.flush();return 1;}}
