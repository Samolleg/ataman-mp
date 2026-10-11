// Production-linked measurement helper. Run only on private fixtures under the UI lock.
// Benchmark mode skips owner image hashing so first paint/save uses the post-open residency.
#include <QtWidgets>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#define private public
#define protected public
#include "view.h"
#include "terrain_virtual_atlas.h"
#include "resourcemanager.h"
#include "tile_form.h"
#include "landscape.h"
#undef private
#undef protected
#include "main_window.h"
#include "terrain_atlas_archive_binding.h"
#include "terrain_atlas_shape_policy.h"
#include "landscape.h"
#include "camera.h"
#include "tile.h"
#include "terrain_semantic_field_store.h"
static void installAudit(){
 qApp->setProperty("auditStart",QDateTime::currentMSecsSinceEpoch());
 auto timer=new QTimer(qApp);timer->setInterval(100);auto attempts=new int(0);
 QObject::connect(timer,&QTimer::timeout,[timer,attempts](){
  if(++*attempts>500){QCoreApplication::exit(20);return;}
  MainWindow* window=nullptr;for(auto w:QApplication::topLevelWidgets())if((window=qobject_cast<MainWindow*>(w)))break;
  if(!window||!window->view())return;
  if(window->view()->m_mprDocument.originalBytes().isEmpty()&&window->view()->m_pendingTerrainAutoCatalogMprPath.filePath().isEmpty())return;
  auto v=window->view();if(!v->ensureTerrainAutoCatalogReady())return;
  timer->stop();
  v->setFixedSize(1024,768);window->adjustSize();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  const auto requestedCamera=QJsonDocument::fromJson(qEnvironmentVariable("EI_AUDIT_CAMERA").toUtf8()).array();
  if(requestedCamera.size()==9)v->m_cam->setLocation(QVector3D(requestedCamera[0].toDouble(),requestedCamera[1].toDouble(),requestedCamera[2].toDouble()),QVector3D(requestedCamera[3].toDouble(),requestedCamera[4].toDouble(),requestedCamera[5].toDouble()),QVector3D(requestedCamera[6].toDouble(),requestedCamera[7].toDouble(),requestedCamera[8].toDouble()));
  const auto look=QJsonDocument::fromJson(qEnvironmentVariable("EI_AUDIT_LOOK_AT").toUtf8()).array();
  if(look.size()==3){QVector3D point(look[0].toDouble(),look[1].toDouble(),look[2].toDouble());v->m_pLand->projectPt(point,true);v->m_cam->moveTo(point);v->m_cam->moveAwayOn(14);}
  v->updateGL();
  if(qEnvironmentVariable("EI_AUDIT_FORCE_FRAME")=="1"){v->makeCurrent();v->resizeGL(v->width(),v->height());v->paintGL();v->swapBuffers();}
  QJsonObject out;QVector3D position,pivot,rotation;v->m_cam->getLocation(position,pivot,rotation);
  out["camera"]=QJsonArray{position.x(),position.y(),position.z(),pivot.x(),pivot.y(),pivot.z(),rotation.x(),rotation.y(),rotation.z()};
  const QString screenshot=qEnvironmentVariable("EI_AUDIT_PNG");if(!screenshot.isEmpty()){const auto image=v->grabFrameBuffer(false);if(image.isNull()||!image.save(screenshot)){QCoreApplication::exit(23);return;}out["framebufferWidth"]=image.width();out["framebufferHeight"]=image.height();}
CValidationReport report;auto p=v->m_terrainAtlasProfile;
  bool exact=p.validateBinding(v->m_mprDocument.mapInfo().innerNamespace,p.atlasPixelSha256,v->m_authoredTerrainTileImages.size(),&report)&&p.approved&&v->m_terrainAtlasAnalysis.usable;
  auto name=STerrainSemanticFieldDocument::memberName(p.mprNamespace,QFileInfo(qEnvironmentVariable("EI_AUDIT_MAP")));QMap<QString,QByteArray> members;QString error;
  bool found=CTerrainAtlasArchiveBindingResolver::readArchiveMembers(v->m_activeTerrainArchivePath,{name},&members,nullptr,&error);STerrainSemanticFieldDocument field;
  auto fieldBytes=v->m_mprDocument.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());bool embedded=!fieldBytes.isEmpty();if(!embedded)fieldBytes=members.value(name);bool present=!fieldBytes.isEmpty();bool decoded=present&&STerrainSemanticFieldDocument::fromBytes(fieldBytes,&field,&report,name);QVector<qint32> land,overlay,baseline;
  v->m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&land,&baseline);v->m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Overlay,&overlay,&baseline);
  bool vertices=decoded&&land==field.land&&overlay==field.overlay;
  QJsonObject stableProfile=QJsonDocument::fromJson(p.toBytes()).object();stableProfile.remove("atlasPixelSha256");stableProfile.remove("mprNamespace");out["profileWithoutAtlasSha256"]=QString(QCryptographicHash::hash(QJsonDocument(stableProfile).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());out["namespace"]=p.mprNamespace;out["approved"]=p.approved;out["usable"]=v->m_terrainAtlasAnalysis.usable;out["profileSource"]=v->m_terrainAtlasProfileSource.path;out["activeArchive"]=v->m_activeTerrainArchivePath;out["families"]=p.families.size();out["physicalTiles"]=v->m_authoredTerrainTileImages.size();out["atlasPixelSha256"]=QString(p.atlasPixelSha256);out["semanticLandVertices"]=land.size();out["semanticOverlayVertices"]=overlay.size();out["semanticVerticesExact"]=vertices;out["exactProfile"]=exact;out["report"]=report.toText();out["error"]=error;
  auto expected=qEnvironmentVariable("EI_AUDIT_ARCHIVE");bool source=QFileInfo(expected).absoluteFilePath().compare(QFileInfo(v->m_activeTerrainArchivePath).absoluteFilePath(),Qt::CaseInsensitive)==0;
  bool expectedFailure=qEnvironmentVariable("EI_AUDIT_EXPECT_SEMANTIC_FAILURE")=="1";
  bool generic=qEnvironmentVariable("EI_AUDIT_GENERIC")=="1";bool loaded=v->m_pLand&&v->m_pLand->isMprLoad()&&!v->m_mprDocument.originalBytes().isEmpty()&&!v->m_authoredTerrainTileImages.isEmpty();bool ok=loaded&&(generic||(source&&exact&&(expectedFailure?!vertices:vertices)));out["loaded"]=loaded;out["semanticPresent"]=present;out["semanticDecoded"]=decoded;out["semanticEmbedded"]=embedded;out["paletteLandChoices"]=v->m_modernLandPaintChoices.size();out["paletteOverlayChoices"]=v->m_modernOverlayPaintChoices.size();out["mprSha256"]=QString(QCryptographicHash::hash(v->m_mprDocument.originalBytes(),QCryptographicHash::Sha256).toHex());
  out["semanticExpectedFailure"]=expectedFailure;out["landFieldSha256"]=QString(QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(land.constData()),land.size()*4),QCryptographicHash::Sha256).toHex());out["overlayFieldSha256"]=QString(QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(overlay.constData()),overlay.size()*4),QCryptographicHash::Sha256).toHex());
  out["readyMs"]=double(QDateTime::currentMSecsSinceEpoch()-qApp->property("auditStart").toLongLong());
  out["animations"]=v->m_mprDocument.mapInfo().animatedTiles.size();out["sectors"]=v->m_mprDocument.sectors().size();out["nativeMaxTilePx"]=terrainAtlasMaximumNativeTileSize(v->m_authoredTerrainTileImages).width();
  out["renderTilePx"]=v->m_pLand->glTexture()?v->m_pLand->glTexture()->width()/CTile::preferredAtlasColumns(v->m_pLand->textureNum())/8:0;out["status"]=ok?"PASS":"FAIL";QFile readyFile(qEnvironmentVariable("EI_AUDIT_OUTPUT")+".ready");if(readyFile.open(QIODevice::WriteOnly)){readyFile.write(QJsonDocument(out).toJson());readyFile.close();}

  if(qEnvironmentVariable("EI_HDMEM_BENCHMARK") != "1") {
  QSet<const uchar*> allBuffers;
  QJsonObject owners;
  auto account=[&](const QString& label,const QVector<QImage>& images){
    QSet<const uchar*> local; qint64 logical=0,unique=0,exclusive=0,mappedBytes=0;
    QSet<QByteArray> hashes;
    for(const QImage& i:images) {if(i.isNull())continue;logical+=i.sizeInBytes();
      if(!local.contains(i.constBits())){local.insert(i.constBits());unique+=i.sizeInBytes();
#ifdef Q_OS_WIN
        MEMORY_BASIC_INFORMATION memoryInfo={};if(VirtualQuery(i.constBits(),&memoryInfo,sizeof(memoryInfo)) && memoryInfo.Type==MEM_MAPPED)mappedBytes+=i.sizeInBytes();
#endif
      }
      if(!allBuffers.contains(i.constBits())){allBuffers.insert(i.constBits());exclusive+=i.sizeInBytes();}
      hashes.insert(CTerrainAtlasProfileInference::pixelSha256({i}));
    }
    owners[label]=QJsonObject{{"entries",images.size()},{"logicalBytes",double(logical)},{"uniqueBuffers",local.size()},{"uniqueBytes",double(unique)},{"additionalBytes",double(exclusive)},{"mappedBytes",double(mappedBytes)},{"pixelIdentities",hashes.size()}};
  };
  account("picker",v->m_pTileForm->tileImages());
  account("viewAuthored",v->m_authoredTerrainTileImages);
  account("prevalidated",v->m_prevalidatedTerrainEmbeddedBinding.decodedAtlasTiles);
  account("globalMaterialSources",v->m_terrainGlobalMaterialSources.values().toVector());
  if(v->m_workingTerrainAtlas){auto w=v->m_workingTerrainAtlas;
    account("workingAuthored",w->m_authoredImages);account("workingSlots",w->m_workingImages);
    if(w->m_resolver){account("resolverAuthored",w->m_resolver->m_authoredTiles);account("resolverMaterialSources",w->m_resolver->m_materialSources.values().toVector());}
  }
  auto resources=CTextureList::getInstance();
  account("resourceCpuImages",resources->m_aTextureImages.values().toVector());
  account("catalogTiles",resources->m_catalogTerrainTileCache.values().toVector());
  auto choiceImages=[](const QVector<STerrainPaintMaterialChoice>& choices){
    QVector<QImage> images;images.reserve(choices.size());
    for(const auto& choice:choices)images.append(choice.thumbnail);
    return images;
  };
  account("landChoiceThumbnails",choiceImages(v->m_modernLandPaintChoices));
  account("underwaterChoiceThumbnails",choiceImages(v->m_modernUnderwaterPaintChoices));
  account("overlayChoiceThumbnails",choiceImages(v->m_modernOverlayPaintChoices));
  account("globalUniformTiles",v->m_terrainGlobalUniformTileCache.values().toVector());
  account("globalTransitionTiles",v->m_terrainGlobalTransitionTileCache.values().toVector());
  account("hydrationBestThumbnails",v->m_terrainGlobalHydrationBestThumbnails.values().toVector());
  out["globalBoundAtlasMetadataEntries"]=v->m_terrainGlobalBoundAtlasCache.size();
  QVector<QImage> stageImages;
  if(v->m_preparedTerrainFamilyStage){const auto stage=v->m_preparedTerrainFamilyStage;
    for(const auto& item:stage->importedTransitions)stageImages.append(item.second);
    stageImages+=stage->decodedTileCache.values().toVector();
    stageImages+=stage->uniformTileCache.values().toVector();
    stageImages+=stage->materialSources.values().toVector();
  }
  account("preparedFamilyStage",stageImages);
  int icons=0;qint64 iconSizeEstimate=0;
  for(const QIcon& icon:v->m_pTileForm->m_icoList)if(!icon.isNull()){
    ++icons;for(const QSize& size:icon.availableSizes())iconSizeEstimate+=qint64(size.width())*size.height()*4;
  }
  out["pickerIconCount"]=icons;out["pickerIconSizeEstimate"]=double(iconSizeEstimate);
  out["owners"]=owners;
  out["cpuAtlasUniqueBuffers"]=allBuffers.size();
  out["authoredDecodedSha256"]=QString(CTerrainAtlasProfileInference::pixelSha256(v->m_authoredTerrainTileImages));
  out["baseGlWidth"]=v->m_pLand->glTexture()->width();out["baseGlHeight"]=v->m_pLand->glTexture()->height();
  out["baseGlMipLevels"]=v->m_pLand->glTexture()->mipLevels();
  out["baseGlFormat"]=int(v->m_pLand->glTexture()->format());

  }
  if(qEnvironmentVariable("EI_HDMEM_BENCHMARK")=="1") {
    const QString root=QFileInfo(qEnvironmentVariable("EI_HDMEM_ROOT")).canonicalFilePath()+"/";
    const QString map=QFileInfo(v->m_pLand->filePath()).canonicalFilePath(), archive=QFileInfo(v->m_activeTerrainArchivePath).canonicalFilePath();
    if(root.size()<15 || !map.startsWith(root,Qt::CaseInsensitive) || !archive.startsWith(root,Qt::CaseInsensitive)){QCoreApplication::exit(31);return;}
    STerrainTileBrush brush;brush.centerX=v->m_terrainEdit.tileCountX()/2.0f;brush.centerY=v->m_terrainEdit.tileCountY()/2.0f;brush.radius=6;brush.shape=ETerrainTileBrushShape::Square;
    int slot=0;
    for(const auto& family:v->m_terrainAtlasProfile.families)if(family.layer==ETerrainAtlasLayer::Land && family.thumbnailSlot>=0){slot=family.thumbnailSlot;break;}
    auto command=v->m_terrainEdit.makeTilePaint(brush,ETerrainTileLayer::Land,slot/64,slot%64,0);
    if(command.deltas.isEmpty()){QCoreApplication::exit(32);return;}
    QJsonArray paintTimes; QElapsedTimer measured;
    v->makeCurrent();
    for(int pass=0;pass<20;++pass){measured.start();if(!v->applyTerrainTilePreview(command,true)){QCoreApplication::exit(33);return;}paintTimes.append(double(measured.nsecsElapsed())/1e6);if(!v->applyTerrainTilePreview(command,false)){QCoreApplication::exit(34);return;}}
    if(!v->applyTerrainTilePreview(command,true)){QCoreApplication::exit(35);return;}
    out["paintMs"]=paintTimes;out["paintChangedCells"]=command.deltas.size();
    measured.start();const bool saved=v->saveLandscapeDocument(QFileInfo(map));out["saveMs"]=double(measured.nsecsElapsed())/1e6;
    if(!saved){QCoreApplication::exit(36);return;}
    QFile savedFile(map);if(!savedFile.open(QIODevice::ReadOnly)){QCoreApplication::exit(37);return;}
    CMprDocument reopened;CValidationReport readbackReport;const auto savedBytes=savedFile.readAll();
    if(!reopened.loadBytes(savedBytes,map,&readbackReport) || reopened.sectors().size()!=v->m_mprDocument.sectors().size()){QCoreApplication::exit(38);return;}
    out["savedMprSha256"]=QString(QCryptographicHash::hash(savedBytes,QCryptographicHash::Sha256).toHex());
    out["savedReadback"]=true;
    QJsonArray fullTimes;const auto images=v->m_authoredTerrainTileImages;const int count=images.size()/64, columns=CTile::preferredAtlasColumns(count), rows=(count+columns-1)/columns;
    v->makeCurrent();for(int pass=0;pass<3;++pass){measured.start();if(!v->m_pLand->previewTerrainAtlasFullUpload(images,count,columns,rows)){QCoreApplication::exit(39);return;}fullTimes.append(double(measured.nsecsElapsed())/1e6);}
    out["fullAtlasUploadMs"]=fullTimes;v->doneCurrent();
  }
QFile f(qEnvironmentVariable("EI_AUDIT_OUTPUT"));if(!f.open(QIODevice::WriteOnly)){QCoreApplication::exit(21);return;}f.write(QJsonDocument(out).toJson());f.close();QTimer::singleShot(1000,qApp,[window,ok](){window->close();QCoreApplication::exit(ok?0:22);});
 });timer->start();
}
Q_COREAPP_STARTUP_FUNCTION(installAudit)
#include "verified_clipboard.h"
namespace ei { bool auditTestClipboardRequested(int,char**) {return true;} }
#define testClipboardRequested auditTestClipboardRequested
#define main ataman_original_main
#include "../source/app/main.cpp"
#undef main
#undef testClipboardRequested
int main(int argc,char**argv){QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,QDir::currentPath()+"/isolated-settings");QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,QDir::currentPath()+"/isolated-settings");return ataman_original_main(argc,argv);}
