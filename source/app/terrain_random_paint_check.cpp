#include "terrain_random_paint_check.h"
#include "terrain_edit_model.h"
#include "terrain_atlas_profile.h"
#include "terrain_semantic_field_store.h"
#include "terrain_atlas_review_dialog.h"
#include "landscape_tools.h"
#include "mpr_document.h"
#include "res_archive_document.h"
#include "endurance_document.h"
#include "view.h"
#include "camera.h"
#include "landscape.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "tree_view.h"
#include "terrain_virtual_atlas.h"
#include "terrain_mixed_edge.h"
#include "terrain_hd_library.h"
#include "resourcemanager.h"
#include "mmp_codec.h"
#include <QApplication>
#include <QTableWidget>
#include <QDataStream>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QPainter>
#include <QTransform>
#include <QTextStream>
#include <QCheckBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSettings>
#include <QUndoStack>
#include <QtEndian>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QMessageBox>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QPointer>
#include <QMouseEvent>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <QKeyEvent>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QGridLayout>
#include <climits>

namespace {
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
bool write(const QString& path, const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
STerrainTransitionSignature signature(qint32 a, qint32 b, qint32 c, qint32 d)
{ STerrainTransitionSignature s; s.northWest=a; s.northEast=b; s.southEast=c; s.southWest=d; return s; }
quint16 packed(int slot, int rotation=0) { SMprPackedTile p; p.atlasIndex=quint8(slot/64);p.tileIndex=quint8(slot%64);p.rotation=quint8(rotation);return p.encode(); }
QByteArray syntheticMpr()
{
    QByteArray mp; QDataStream out(&mp,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out << quint32(0xCE4AF672) << float(100) << quint32(1) << quint32(1)
        << quint32(1) << quint32(256) << quint32(64) << quint32(32) << quint16(1) << quint32(1);
    out << qint32(1);for(int i=0;i<10;++i) out << float(i==3?1:0);
    for(int i=0;i<64;++i) out << qint32(i==2?2:(i==3?3:1));
    out << quint16(5) << quint16(2);
    QByteArray sector;QDataStream sec(&sector,QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);sec << quint32(0xCF4BF774) << quint8(3);
    sector.append(QByteArray(2*33*33*8+3*16*16*2,'\0'));
    QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019CE23C,reinterpret_cast<uchar*>(empty.data()));
    qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
    CResArchiveDocument archive;QStringList errors;QByteArray first,result;
    if(!archive.loadBytes(empty,"empty",&errors)
       || !archive.buildWithAppendedMember("Random.MP",mp,0,&first,&errors)
       || !archive.loadBytes(first,"first",&errors)
       || !archive.buildWithAppendedMember("Random000000.SEC",sector,0,&result,&errors)) return {};
    return result;
}
QByteArray snapshot(const CTerrainEditModel& model)
{
    QByteArray bytes;
    for(int y=0;y<model.tileCountY()/16;++y) for(int x=0;x<model.tileCountX()/16;++x) bytes+=model.sectorPayload(x,y);
    return bytes;
}
QImage preview(const CTerrainEditModel& model, const QVector<QImage>& images, const QRect& region)
{
    const int size=qMin(64,images.first().width());
    QImage result(region.width()*size,region.height()*size,QImage::Format_RGB32);result.fill(Qt::magenta);
    QPainter painter(&result);
    for(int y=region.top();y<=region.bottom();++y)for(int x=region.left();x<=region.right();++x) {
        bool found=false;auto tile=SMprPackedTile::decode(model.packedTile(x,y,ETerrainTileLayer::Land,&found));
        if(found && tile.atlasSlot()<images.size()) painter.drawImage(QRect((x-region.x())*size,(region.bottom()-y)*size,size,size),
            images[tile.atlasSlot()].transformed(QTransform().rotate(-90*tile.rotation)));
    }
    return result;
}
class RandomCommand : public QUndoCommand {
public:
    RandomCommand(CTerrainEditModel* model,const STerrainTileCommand& command):m_model(model),m_command(command){}
    void undo() override { valid &= m_model->apply(m_command,false); }
    void redo() override { valid &= m_model->apply(m_command,true); }
    bool valid{true};
private:
    CTerrainEditModel* m_model;STerrainTileCommand m_command;
};
class HiddenCheckWindows : public QObject {
public:
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(event->type()==QEvent::Polish)
            if(auto* widget=qobject_cast<QWidget*>(watched))
                if(widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
}

int runTerrainRandomPaintCheck(const QString& fixturePath)
{
    QTextStream log(stdout);log.setCodec("UTF-8");int passed=0,failed=0;
    auto check=[&](bool condition,const QString& name) {log<<(condition?"PASS ":"FAIL ")<<name<<'\n';log.flush();condition?++passed:++failed;return condition;};
    const auto fixture=QJsonDocument::fromJson(read(fixturePath)).object();
    HiddenCheckWindows hiddenWindows;
    if(fixture.value("ownerPaintRegression").toBool() || fixture.value("paintMatrix").toBool() || fixture.value("hdPublication").toBool())qApp->installEventFilter(&hiddenWindows);
    const QString output=fixture.value("output").toString();
    if(!check(!output.isEmpty() && QDir(output).exists(),"owned output directory")) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,output+"/settings");
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,output+"/settings");
    if(fixture.value("paletteRecovery").toBool()) {
        // Use the production first-frame/event-loop path, never ensure() from
        // the harness. Resource roots and the map must be private copies.
        QDir::setCurrent(output);
        CSettings settings;
        const QString root=fixture.value("realRoot").toString();
        auto* roots=dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"));
        if(!check(roots && QFileInfo(root+"/res/HDPack_Terrain.res").isFile(),"copied atlas resource root"))return 1;
        QVector<QString> probeResources={root+"/res"};
        for(const auto& extra:fixture.value("readOnlyResourceRoots").toArray())probeResources.append(extra.toString());
        roots->setValue(probeResources);
        if(fixture.value("hdPublication").toBool())
            if(auto* profile=dynamic_cast<COptString*>(settings.opt("resourceProfile")))profile->setValue("deja-vu");
        if(fixture.value("ownerPaintRegression").toBool() || fixture.value("paintMatrix").toBool() || fixture.value("hdPublication").toBool()) {
            if(auto* lighting=dynamic_cast<COptInt*>(settings.opt("renderLightingMode")))lighting->setValue(0);
            if(auto* culling=dynamic_cast<COptBool*>(settings.opt("frustumCulling")))culling->setValue(false);
        }
        CTextureList::getInstance()->attachSettings(&settings);
        CObjectList::getInstance()->attachSettings(&settings);
        QUndoStack history;CTreeView tree;QTableWidget properties;properties.setColumnCount(2);
        CProgressView progress;QLineEdit coordinates;CTileForm tiles;CLandscapeTools panel;
        panel.setAttribute(Qt::WA_DontShowOnScreen);panel.resize(360,900);panel.show();
        CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
        // Match CMainWindow's production viewport setup: Qt otherwise drops
        // no-button hover events before they can update the displayed brush.
        view.setMouseTracking(true);
        view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
        view.attachLandscapeTools(&panel);view.resize(640,480);view.show();QApplication::processEvents();
        if(!check(view.isValid(),"hidden desktop OpenGL context"))return 1;
        const QString map=root+"/Maps/"+fixture.value("zone").toString("zone1")+".mpr";
        QElapsedTimer timer;timer.start();view.loadLandscape(QFileInfo(map),false);
        const qint64 openMs=timer.elapsed();
        while(timer.elapsed()<120000 && (view.m_terrainAutoCatalogPending || view.m_terrainAutoCatalogBuilding)) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,50);QThread::msleep(5);
        }
        log<<"TIMING openReturnMs="<<openMs<<" paletteReadyMs="<<timer.elapsed()<<" firstFrameMs="<<view.m_terrainAutoCatalogFirstFrameMs<<'\n';
        if(!check(!view.m_terrainAutoCatalogPending && !view.m_terrainAutoCatalogBuilding && view.m_terrainEditAvailable,
                  "catalog fills automatically after a frame without harness ensure"))return 1;
        QMap<qint32,QImage> initialPreviews;
        for(const auto& choice:view.m_modernLandPaintChoices)
            if(!choice.thumbnail.isNull())initialPreviews.insert(choice.id,choice.thumbnail);
        QElapsedTimer thumbnails;thumbnails.start();
        while(thumbnails.elapsed()<120000 && (view.m_terrainGlobalHydrationPendingJobs>0 || view.m_terrainGlobalHydrationPublishQueued)) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,50);QThread::msleep(5);
        }
        log<<"TIMING thumbnailWaitMs="<<thumbnails.elapsed()<<'\n';
        bool primaryStable=true;
        for(const auto& choice:view.m_modernLandPaintChoices)
            if(initialPreviews.contains(choice.id))primaryStable &= choice.thumbnail==initialPreviews.value(choice.id);
        check(primaryStable,"authored hydration preserves existing primary artwork");
        if(fixture.value("localPalette").toBool()) {
            const auto local=TerrainHdLibrary::availableSources();
            check(!local.isEmpty(),"local palette library available");
            bool localOnly=!view.m_terrainGlobalBoundAtlasCache.isEmpty();
            for(const auto& binding:view.m_terrainGlobalBoundAtlasCache)
                localOnly &= local.contains(binding.archivePath);
            check(localOnly,"every global palette binding comes from local library");
            const auto beforeLand=view.m_modernLandPaintChoices;
            const auto beforeWater=view.m_modernUnderwaterPaintChoices;
            const auto beforeOverlay=view.m_modernOverlayPaintChoices;
            panel.resize(760,1100);panel.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
            QApplication::processEvents();
            for(auto* button:panel.findChildren<QToolButton*>())
                if(button->property("terrainMaterialId").isValid()&&button->isEnabled()
                   &&button->toolTip().contains("Grass / Dark")){button->click();break;}
            // Resize schedules a deferred responsive relayout; capture only
            // settled native geometry, not an intermediate synthetic frame.
            QElapsedTimer settle;settle.start();while(settle.elapsed()<250) {
                QApplication::processEvents();QThread::msleep(5);
            }
            auto* toolScroll=panel.findChild<QScrollArea*>("landscapeToolScroll");
            const int scrollBefore=toolScroll->verticalScrollBar()->value();
            check(panel.grab().save(output+"/palette-before.png"),"native palette before capture");
            QVector<QPointer<QToolButton>> buttons;
            for(auto* button:panel.findChildren<QToolButton*>())
                if(button->property("terrainMaterialId").isValid())buttons.append(button);
            // Same production catalog rebuild called by HD adoption, without
            // another multi-minute regeneration of unchanged texture artwork.
            view.rebuildTerrainAutoCatalog();settle.restart();while(settle.elapsed()<250) {
                QApplication::processEvents();QThread::msleep(5);
            }
            check(toolScroll->verticalScrollBar()->value()==scrollBefore,"catalog rebuild preserves outer panel scroll");
            bool retained=!buttons.isEmpty();for(const auto& button:buttons)retained &= !button.isNull();
            check(retained,"palette keeps native buttons across atlas catalog rebuild");
            const auto stable=[&](const QVector<STerrainPaintMaterialChoice>& before,
                                  const QVector<STerrainPaintMaterialChoice>& after) {
                int ready=0;bool same=before.size()==after.size();
                for(const auto& old:before)for(const auto& now:after)if(old.id==now.id) {
                    if(!old.thumbnail.isNull()){++ready;same &= old.thumbnail==now.thumbnail;}
                    if(old.authoredThumbnails.size()!=now.authoredThumbnails.size())same=false;
                    else for(int i=0;i<old.authoredThumbnails.size();++i)
                        if(!old.authoredThumbnails[i].isNull())same &= old.authoredThumbnails[i]==now.authoredThumbnails[i];
                }
                return same&&ready>0;
            };
            check(stable(beforeLand,view.m_modernLandPaintChoices),"ground thumbnails and variants never reset");
            check(stable(beforeWater,view.m_modernUnderwaterPaintChoices),"underwater thumbnails and variants never reset");
            check(stable(beforeOverlay,view.m_modernOverlayPaintChoices),"overlay thumbnails and variants never reset");
            check(view.m_terrainGlobalHydrationPendingJobs==0,"warm rebuild queues no archive rehydration");
            check(panel.grab().save(output+"/palette-after.png"),"native palette after capture");
            log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
        }
        if(fixture.value("hdPublication").toBool()) {
            if(!check(root.contains("/build-tests-157-hd/"),"HD publication restricted to private fixture"))return 1;
            const bool distribute=fixture.value("distribute").toBool();
            const bool saveOnly=fixture.value("hdSaveOnly").toBool();
            const bool rollbackProbe=fixture.value("saveRollbackProbe").toBool();
            bool rollbackDialog=false;
            const QString oldNamespace=view.m_mprDocument.mapInfo().innerNamespace;
            const auto animations=[&](){QVector<QImage> frames;for(const auto& a:view.m_mprAnimatedTiles)
                for(int f=0;f<a.frameCount;++f)frames.append(view.m_authoredTerrainTileImages.value(a.tileIndex+f));return frames;};
            const auto oldAnimations=animations();
            const auto oldAnimationRecords=view.m_mprAnimatedTiles;
            const auto sameAnimationPixels=[](const QVector<QImage>& a,const QVector<QImage>& b) {
                if(a.size()!=b.size())return false;
                for(int i=0;i<a.size();++i)
                    if(a[i].isNull()||b[i].isNull()
                       ||a[i].convertToFormat(QImage::Format_RGBA8888)!=b[i].convertToFormat(QImage::Format_RGBA8888))return false;
                return true;
            };
            const auto describeAnimations=[&](const QString& phase) {
                const auto frames=animations();
                for(int i=0;i<frames.size();++i) {
                    const auto rgba=frames[i].convertToFormat(QImage::Format_RGBA8888);
                    log<<"HD ANIMATION "<<phase<<" frame="<<i<<" size="<<rgba.width()<<"x"<<rgba.height()
                       <<" format="<<int(frames[i].format())<<" rgba="
                       <<QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(rgba.constBits()),int(rgba.sizeInBytes())),QCryptographicHash::Sha256).toHex()<<"\n";
                }
                for(const auto& a:view.m_mprAnimatedTiles)log<<"HD ANIMATION RANGE "<<phase<<" start="<<a.tileIndex<<" frames="<<a.frameCount<<"\n";
                log.flush();
            };
            describeAnimations("before");
            QVector<QVector3D> points;
            if(saveOnly)points={QVector3D(73,1,0),QVector3D(139,1,0),QVector3D(451,9,0),QVector3D(349,123,0)};
            if(fixture.value("legacyHd").toBool())
                points={QVector3D(25,25,0),QVector3D(55,45,0),QVector3D(65,75,0)};
            if(fixture.value("legacyHd").toBool()) {
                bool found=false;
                for(int y=0;y<view.m_terrainEdit.tileCountY()&&!found;++y)
                    for(int x=0;x<view.m_terrainEdit.tileCountX()&&!found;++x)
                        if(SMprPackedTile::decode(view.m_terrainEdit.packedTile(x,y,ETerrainTileLayer::Land)).atlasSlot()==915) {
                            points.append(QVector3D(x*2+1,y*2+1,0));found=true;
                            log<<"HD VIEW former refusal slot915 centre="<<x*2+1<<","<<y*2+1<<'\n';
                        }
            }
            if(!saveOnly&&!fixture.value("legacyHd").toBool())for(int slot:{34,44,215,666}) {
                bool found=false;
                for(int y=0;y<view.m_terrainEdit.tileCountY()&&!found;++y)
                    for(int x=0;x<view.m_terrainEdit.tileCountX()&&!found;++x)
                        if(SMprPackedTile::decode(view.m_terrainEdit.packedTile(x,y,ETerrainTileLayer::Land)).atlasSlot()==slot) {
                            points.append(QVector3D(x*2+1,y*2+1,0));found=true;
                            log<<"HD VIEW slot="<<slot<<" centre="<<x*2+1<<","<<y*2+1<<"\n";
                        }
                check(found,QString("real map uses review slot %1").arg(slot));
            }
            const auto capture=[&](const QString& phase){
                view.m_fogVisible=false;view.m_sceneLightsVisible=false;view.m_bDrawWater=false;
                view.m_bDrawLand=true;view.m_bPreviewTile=false;view.m_tileGridVisible=false;
                view.resize(1024,1024);QApplication::processEvents();
                for(int i=0;i<points.size();++i) {
                    view.makeCurrent();view.resizeGL(1024,1024);view.m_cam->setLocation(points[i]+QVector3D(0,0,256),QVector3D(),QVector3D());
                    view.m_projection.setToIdentity();view.m_projection.ortho(-5,5,-5,5,1,512);
                    QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
                    QOpenGLFramebufferObject target(QSize(1024,1024),format);view.m_sceneReviewTarget=&target;
                    view.draw();QOpenGLContext::currentContext()->functions()->glFinish();
                    const auto frame=target.toImage();view.m_sceneReviewTarget=nullptr;target.release();
                    check(view.m_landProgram.isLinked()&&!frame.isNull()&&frame.save(output+QString("/%1-%2-viewport.png").arg(phase).arg(i)),"HD native viewport "+phase);
                }
            };
            capture("before");
            bool confirmed=false;QByteArray publishedBaseline;bool oldSeed=false;quint32 oldSeedValue=0;
            QObject::connect(&view,&CView::terrainPublicationPreflightRequested,&view,[&](const QString& path,bool* accepted){if(accepted)*accepted=QDir::cleanPath(path)==QDir::cleanPath(root+"/res/HDPack_Terrain.res");});
            QElapsedTimer publicationClock;int publicationTicks=0;qint64 lastPublicationTick=0,maxPublicationGap=0;QSet<QString> publicationLabels;
            QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,&view,[&](){
                if(fixture.value("publicationProgress").toBool()&&confirmed)for(auto* w:QApplication::topLevelWidgets()) {
                    if(w->objectName()!=QStringLiteral("terrainHdProgressDialog")||!w->isVisible())continue;
                    auto* cancel=w->findChild<QPushButton*>("hdProgressCancel");auto* label=w->findChild<QLabel*>("hdProgressLabel");
                    if(!cancel||cancel->isEnabled()||!label)continue;
                    if(!publicationClock.isValid())publicationClock.start();
                    const auto now=publicationClock.elapsed();maxPublicationGap=qMax(maxPublicationGap,now-lastPublicationTick);lastPublicationTick=now;++publicationTicks;
                    const auto text=label->text().section('\n',0);if(!publicationLabels.contains(text)){publicationLabels.insert(text);log<<"PUBLICATION "<<text<<"\n";log.flush();}
                }
                for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)) {
                    auto* option=box->findChild<QCheckBox*>("terrainHdDistributeVariants");
                    if(option&&!confirmed) {
                        check(!option->isChecked(),"HD distribute checkbox defaults OFF");
                        option->setChecked(distribute);confirmed=true;box->button(QMessageBox::Yes)->click();
                    } else if(!option) {
                        log<<"HD DIALOG "<<box->text()<<"\n";log.flush();
                        if(box->icon()==QMessageBox::Critical){
                            if(rollbackProbe && box->text().contains("HDPack_Terrain.res rollback: complete")) rollbackDialog=true;
                            else ++failed;
                            box->reject();
                        }
                        else if(box->standardButtons()&QMessageBox::Save)box->button(QMessageBox::Save)->click();
                        else box->accept();
                    }
                }
            });dialogs.start(50);
            if(fixture.value("legacySaveOnly").toBool()) {
                QString error;
                const auto oldBytes=read(map);
                STerrainTileBrush brush;brush.centerX=12;brush.centerY=12;brush.radius=0;
                const auto packed=SMprPackedTile::decode(view.m_terrainEdit.packedTile(12,12,ETerrainTileLayer::Land));
                const auto command=view.m_terrainEdit.makeTilePaint(brush,ETerrainTileLayer::Land,
                    packed.atlasIndex,packed.tileIndex,(packed.rotation+1)%4);
                check(!command.isEmpty()&&view.m_terrainEdit.apply(command,true),"ordinary authored tile rotation changes legacy map");
                view.m_pLand->setDirty(true);
                check(view.m_worldTerrainSession.load(map,&error),"retain legacy map for shared brush");
                view.syncWorldTerrainActive();
                check(view.m_worldTerrainSession.dirtyPaths().size()==1,"legacy single-map world save repro");
                const auto expected=snapshot(view.m_terrainEdit);
                view.saveLand();
                check(!view.m_pLand->isDirty()&&view.m_worldTerrainSession.dirtyPaths().isEmpty(),"legacy normal Save checkpoints both owners");
                check(read(map)!=oldBytes&&view.m_mprDocument.mapInfo().innerNamespace==oldNamespace,"legacy edit saves without unnecessary atlas publication");
                view.unloadLand(false);view.makeCurrent();view.loadLandscape(QFileInfo(map),false);
                check(view.ensureTerrainAutoCatalogReady()&&snapshot(view.m_terrainEdit)==expected,"legacy Save fresh reopen retains exact edit");
                dialogs.stop();log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
            }
            if(saveOnly)view.shuffleTerrainTiles(false);
            else {
                view.republishTerrainAtlasHd();
                if(fixture.value("publicationProgress").toBool()) {
                    log<<"PUBLICATION ticks="<<publicationTicks<<" maxGapMs="<<maxPublicationGap<<"\n";
                    check(publicationTicks>=5&&publicationLabels.size()>=4,"publication remains visible through multiple completion phases");
                    check(maxPublicationGap<3000,"publication GUI timer gap stays below three seconds");
                }
                check(confirmed,"real HD confirmation accepted");
                check(view.m_mprDocument.mapInfo().innerNamespace!=oldNamespace && view.m_terrainAtlasAnalysis.usable,"published MPR/RES adopted by real editor");
            }
            describeAnimations("after");
            check(sameAnimationPixels(animations(),oldAnimations),"all native animation pixels remain exact after adoption");
            check(view.m_mprAnimatedTiles==oldAnimationRecords,"all animation ranges remain exact after adoption");
            capture("after");
            if(distribute) {
                const auto laidOut=snapshot(view.m_terrainEdit);
                const auto seed=view.m_terrainEdit.randomPaintSeed();
                check(view.m_pLand->isDirty()&&history.canUndo(),"checked layout is an undoable dirty edit");
                history.undo();const auto baseline=snapshot(view.m_terrainEdit);
                check(baseline!=laidOut,"Undo restores pre-layout terrain");
                history.redo();check(snapshot(view.m_terrainEdit)==laidOut && view.m_terrainEdit.randomPaintSeed()==seed,"Redo restores exact layout and seed");
                if(rollbackProbe) {
#ifdef Q_OS_WIN
                    const auto originalMpr=read(map);
                    const QString archive=root+"/res/HDPack_Terrain.res";
                    const auto originalResHash=QCryptographicHash::hash(read(archive),QCryptographicHash::Sha256);
                    HANDLE locked=CreateFileW(reinterpret_cast<LPCWSTR>(map.utf16()),GENERIC_READ,
                        FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                    if(!check(locked!=INVALID_HANDLE_VALUE,"MPR replacement denied by test-owned read handle"))return 1;
                    view.saveLand();CloseHandle(locked);
                    check(rollbackDialog,"MPR failure reports completed RES rollback");
                    check(read(map)==originalMpr,"failed MPR save retains original bytes");
                    check(QCryptographicHash::hash(read(archive),QCryptographicHash::Sha256)==originalResHash,
                          "failed MPR save restores the original RES exactly");
                    dialogs.stop();log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
#else
                    check(false,"rollback lock probe requires Windows");return 1;
#endif
                }
                if(fixture.value("singleWorldSave").toBool()) {
                    QString error;
                    check(view.m_worldTerrainSession.load(map,&error),"retain current map in shared brush session");
                    view.syncWorldTerrainActive();
                    check(view.m_worldTerrainSession.dirtyPaths().size()==1,"single active dirty map reproduces owner save path");
                }
                view.saveLand();check(!view.m_pLand->isDirty(),"layout saves through normal MPR path");
                if(fixture.value("singleWorldSave").toBool())
                    check(view.m_worldTerrainSession.dirtyPaths().isEmpty(),"normal Save checkpoints the shared brush session");
            } else check(!view.m_pLand->isDirty(),"unchecked HD conversion adds no layout edit");
            const auto saved=snapshot(view.m_terrainEdit);
            view.unloadLand(false);view.makeCurrent();view.loadLandscape(QFileInfo(map),false);
            check(view.ensureTerrainAutoCatalogReady()&&snapshot(view.m_terrainEdit)==saved,"fresh reopen retains published HD terrain and layout");
            describeAnimations("reopened");
            bool rangesValid=view.m_mprAnimatedTiles.size()==oldAnimationRecords.size();
            for(int i=0;i<view.m_mprAnimatedTiles.size()&&rangesValid;++i)
                rangesValid=view.m_mprAnimatedTiles[i].frameCount==oldAnimationRecords[i].frameCount
                    &&view.m_mprAnimatedTiles[i].tileIndex%64+view.m_mprAnimatedTiles[i].frameCount<=64;
            check(sameAnimationPixels(animations(),oldAnimations)&&rangesValid,"fresh reopen retains native animation artwork and contiguous frame counts");
            capture("reopened");dialogs.stop();
            log<<"RESULT passed="<<passed<<" failed="<<failed<<"\n";return failed?1:0;
        }
        if(fixture.value("paintMatrix").toBool()) {
            // This branch owns copied real MPR/RES inputs. Every edit below enters
            // through Qt mouse/key events; no direct setTile or forced footprint.
            if(!check(root.contains("/build-tests-155-matrix/fixture"),"matrix writes only its private fixture"))return 1;
            panel.activateMode(ELandscapeEditMode::TilePaint,true);
            const auto animationFrames=[&](){QVector<QVector<QImage>> sequences;for(const auto& animation:view.m_mprAnimatedTiles){QVector<QImage> frames;for(int frame=0;frame<animation.frameCount;++frame)frames.append(view.m_pTileForm->tileImages().value(animation.tileIndex+frame));sequences.append(frames);}return sequences;};
            const auto originalAnimationFrames=animationFrames();
            QObject::connect(&view,&CView::updateMsg,[&](const QString& message){log<<"MESSAGE "<<message<<'\n';log.flush();});
            QTimer matrixDialogs;
            QObject::connect(&matrixDialogs,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* b=qobject_cast<QMessageBox*>(w)){log<<"UNEXPECTED "<<b->text()<<'\n';log.flush();++failed;b->reject();}});
            matrixDialogs.start(50);
            const auto settle=[](){QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);};
            const auto button=[&](const char* name){return panel.findChild<QAbstractButton*>(name);};
            const auto click=[&](const char* name){auto* b=button(name);if(check(b && b->isEnabled(),QString("UI %1 available").arg(name)))b->click();settle();};
            for(const char* name:{"modernAutoResolveBlends","modernMixedEdge","randomPaintVariants","generatedHdVariants","randomizeUniformRotation"})
                if(auto* b=button(name))b->setChecked(true);
            auto* radius=panel.findChild<QSlider*>("terrainPaintRadius");radius->setValue(3);
            const auto surface=[&](ELandscapeEditMode mode){
                click("landscapeSurfaceSegment");
                auto* action=panel.findChild<QComboBox*>("landscapeSurfaceAction");
                action->setCurrentIndex(action->findData(int(mode)));settle();
                check(panel.editMode()==mode,"surface action dispatch matches selection");
            };
            const auto choose=[&](const QVector<STerrainPaintMaterialChoice>& choices,const QString& name){
                for(const auto& c:choices)if(c.name.contains(name,Qt::CaseInsensitive) && c.paintAvailable) {
                    log<<"MATERIAL "<<c.id<<' '<<c.name<<" animated="<<c.animated<<'\n';
                    return check(panel.selectPaintTerrainType(c.id),"material "+name+" selected");
                }
                return check(false,"available material "+name);
            };
            for(const auto& c:view.m_modernOverlayPaintChoices)log<<"OVERLAY "<<c.name<<" available="<<c.paintAvailable<<" animated="<<c.animated<<'\n';
            const auto layerBytes=[&](bool ground){
                QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
                if(ground)for(int y=0;y<view.m_terrainEdit.vertexCountY();++y)for(int x=0;x<view.m_terrainEdit.vertexCountX();++x)stream<<view.m_terrainEdit.height(x,y);
                for(int y=0;y<view.m_terrainEdit.tileCountY();++y)for(int x=0;x<view.m_terrainEdit.tileCountX();++x) {
                    stream<<view.m_terrainEdit.packedTile(x,y,ground?ETerrainTileLayer::Land:ETerrainTileLayer::Overlay);
                    if(!ground)stream<<view.m_terrainEdit.overlayMaterialIndex(x,y);
                }
                if(!ground)for(int y=0;y<view.m_terrainEdit.tileCountY()/16;++y)for(int x=0;x<view.m_terrainEdit.tileCountX()/16;++x) {
                    const auto payload=view.m_terrainEdit.sectorPayload(x,y);
                    if(payload.size()>5 && quint8(payload[4])==3)bytes+=payload.mid(5+33*33*8,33*33*8);
                }
                return bytes;
            };
            QVector3D world(33,33,0);
            const auto aim=[&](const QVector3D& destination){
                world=destination;view.m_pLand->projectPt(world,panel.tilePaintEnabled());
                view.moveCameraToPoint(world);view.m_cam->moveAwayOn(35);view.updateGL();settle();
                const auto p=world.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));
                return QPoint(qRound(p.x()),qRound(view.height()-p.y()));
            };
            const auto capture=[&](){
                view.makeCurrent();QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
                QOpenGLFramebufferObject target(view.size(),format);if(!target.isValid() || !target.bind())return QImage();
                auto* gl=QOpenGLContext::currentContext()->functions();gl->glViewport(0,0,view.width(),view.height());
                gl->glEnable(GL_DEPTH_TEST);gl->glDepthMask(GL_TRUE);gl->glClearColor(0,0,0,1);gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                const auto projection=view.m_projection;const bool water=view.m_bDrawWater;
                view.m_projection.setToIdentity();view.m_projection.ortho(-9,9,-7,7,1,100);
                view.m_minimapCaptureActive=true;view.m_minimapCaptureUnlitPass=true;
                view.m_minimapCaptureCameraPosition=world+QVector3D(0,-20,50);
                view.m_minimapCaptureView.setToIdentity();view.m_minimapCaptureView.lookAt(view.m_minimapCaptureCameraPosition,world,QVector3D(0,1,0));
                view.m_bDrawWater=!panel.tilePaintEnabled();view.draw();auto image=target.toImage();
                check(view.m_landProgram.isLinked() && gl->glGetError()==0,"native GL draw succeeds");
                view.m_projection=projection;view.m_bDrawWater=water;view.m_minimapCaptureActive=false;view.m_minimapCaptureUnlitPass=false;
                target.release();return image;
            };
            const auto stroke=[&](const QString& name,const QVector3D& point,bool ground,bool drag,bool requirePixels=true){
                const QPoint start=aim(point);bool found=false;
                const auto hit=view.getTerrainBrushPos(start.x(),start.y(),&found,ground);
                check(view.rect().contains(start) && found,"ray hit "+name);
                log<<"PICK "<<name<<' '<<hit.x()<<','<<hit.y()<<','<<hit.z()<<'\n';
                const auto before=snapshot(view.m_terrainEdit),other=layerBytes(!ground);const auto frameBefore=capture();
                const int count=history.count();
                int changedCenters=0;
                auto previousStamp=before;
                for(int step=0;step<(drag?5:3);++step) {
                    const auto type=step==0?QEvent::MouseMove:step==1?QEvent::MouseButtonPress:step==(drag?4:2)?QEvent::MouseButtonRelease:QEvent::MouseMove;
                    const QPoint pos=start+(step>1&&drag?QPoint((step==2?60:120),0):QPoint());
                    QMouseEvent event(type,pos,(type==QEvent::MouseMove)?Qt::NoButton:Qt::LeftButton,
                        (step>=1 && type!=QEvent::MouseButtonRelease)?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                    QElapsedTimer eventTimer;eventTimer.start();
                    QApplication::sendEvent(&view,&event);
                    const auto eventUs=eventTimer.nsecsElapsed()/1000;
                    settle();
                    log<<"TIMING paint="<<name<<" step="<<step<<" eventUs="<<eventUs
                       <<" includingQueuedUs="<<eventTimer.nsecsElapsed()/1000<<'\n';
                    const auto currentStamp=snapshot(view.m_terrainEdit);
                    if(step>=1 && type!=QEvent::MouseButtonRelease && currentStamp!=previousStamp)++changedCenters;
                    previousStamp=currentStamp;
                    if(fixture.value("tracePaintDrag").toBool() && drag && !panel.sculptEnabled()) {
                        bool stepHit=false;const auto stepPoint=view.getTerrainBrushPos(pos.x(),pos.y(),&stepHit);
                        CTile* debugTile=nullptr;STileLocation debugLocation;STerrainTileBrush debugBrush;QPoint debugVertex;STerrainPaintFootprintSnapshot debugFootprint;
                        const bool resolved=stepHit && view.resolveTerrainPaintFootprint(stepPoint,ground,&debugTile,&debugLocation,&debugBrush,&debugVertex,&debugFootprint);
                        log<<"DRAG "<<name<<" step="<<step<<" hit="<<stepHit<<" xyz="<<stepPoint.x()<<','<<stepPoint.y()<<','<<stepPoint.z()
                           <<" active="<<view.m_tilePaintStrokeActive<<" interrupted="<<view.m_landscapeGestureInterrupted<<" stamps="<<view.m_tileStrokeActions
                           <<" resolved="<<resolved<<" displayed="<<view.m_terrainPaintFootprintValid<<':'<<view.m_terrainPaintFootprint.anchor.x()<<','<<view.m_terrainPaintFootprint.anchor.y()
                           <<" fresh="<<debugFootprint.anchor.x()<<','<<debugFootprint.anchor.y()<<" centerValid="<<view.m_lastTerrainPaintCenterValid<<'\n';log.flush();
                    }
                }
                const auto after=snapshot(view.m_terrainEdit);const auto frameAfter=capture();
                frameBefore.save(output+"/"+name+"-before.png");frameAfter.save(output+"/"+name+"-after.png");
                check(after!=before,"event stroke changes "+name);
                check(layerBytes(!ground)==other,"other layer preserved "+name);
                const auto* worldCommand=view.m_worldTerrainSession.nextCommand(false);
                const bool worldStroke=history.count()==count && worldCommand && worldCommand->localUndoIndex==history.index();
                check(history.count()==count+1 || worldStroke,"one Undo transaction "+name);
                if(drag && !panel.sculptEnabled())check(changedCenters>1,"drag changes several brush centers "+name);
                if(requirePixels)check(!frameBefore.isNull() && frameBefore!=frameAfter,"native pixels change "+name);
                if(!view.worldTerrainHistory(false))history.undo();
                check(snapshot(view.m_terrainEdit)==before,"exact Undo "+name);
                if(fixture.value("waterEyedrop").toBool()) {
                    const auto frame=capture();frame.save(output+"/"+name+"-undo.png");
                    check(frame==frameBefore,"native Undo pixels restored "+name);
                }
                if(!view.worldTerrainHistory(true))history.redo();
                check(snapshot(view.m_terrainEdit)==after,"exact Redo "+name);
                if(fixture.value("waterEyedrop").toBool()) {
                    const auto frame=capture();frame.save(output+"/"+name+"-redo.png");
                    check(frame==frameAfter,"native Redo pixels restored "+name);
                }
                return after!=before;
            };
            if(fixture.value("selectedDistantNeighbor").toBool()) {
                SMapCatalogEntry active;active.zoneId="active";active.allodId="fixture";
                active.mprName=view.m_mprDocument.mapInfo().innerNamespace;
                active.mprPath=map;active.sectorWidth=view.m_mprDocument.mapInfo().sectorCountX;
                active.sectorHeight=view.m_mprDocument.mapInfo().sectorCountY;
                auto neighbor=active;neighbor.zoneId="distant";neighbor.mprPath=root+"/Maps/distant.mpr";
                SWorldMapZoneOrigin origin;origin.profileId="fixture";origin.allodId="fixture";
                origin.zoneId=active.zoneId;origin.mprIdentity=active.mprName;origin.componentAnchorZoneId=active.zoneId;
                auto distantOrigin=origin;distantOrigin.zoneId=neighbor.zoneId;distantOrigin.origin=QPointF(1024,1024);
                view.setNeighborMapContext({active,neighbor},{origin,distantOrigin},"fixture","fixture","active");
                check(view.m_worldTerrainEnabled && view.m_hasActiveMapWorldOrigin
                    && !view.m_worldTerrainEnabledPaths.isEmpty(),"production neighbor context enables shared brush");
            }
            if(fixture.value("waterEyedrop").toBool()) {
                surface(ELandscapeEditMode::OverlayPaint);
                click("circlePaintShape");click("vertexPaintTarget");radius->setValue(1);
                QPoint source(-1,-1),destination(-1,-1);quint16 sample=0;
                const auto images=view.m_workingTerrainAtlas->activeTileImages();
                const auto& catalog=view.m_workingTerrainAtlas->catalog();
                for(int y=12;y<view.m_terrainEdit.tileCountY()-3 && destination.x()<0;++y)
                    for(int x=3;x<view.m_terrainEdit.tileCountX()-3;++x) {
                        bool present=false;
                        const auto tile=view.m_terrainEdit.packedTile(x,y,ETerrainTileLayer::Overlay,&present);
                        bool known=false;const auto s=catalog.gridSignatureForPacked(tile,&known);
                        if(!present || !known || s.northWest!=1522175125 || s.northEast!=s.northWest
                            || s.southEast!=s.northWest || s.southWest!=s.northWest)continue;
                        const auto physical=SMprPackedTile::decode(tile);
                        if(physical.atlasSlot()>=images.size())continue;
                        if(source.x()<0){source=QPoint(x,y);sample=tile;continue;}
                        if((source-QPoint(x,y)).manhattanLength()<8)continue;
                        if(images[physical.atlasSlot()]!=images[SMprPackedTile::decode(sample).atlasSlot()]) {
                            destination=QPoint(x,y);break;
                        }
                    }
                if(!check(destination.x()>=0,"two visually different Water Still tiles in original zone1"))return 1;
                log<<"SAMPLE source="<<source.x()<<','<<source.y()<<" destination="<<destination.x()<<','<<destination.y()<<" packed="<<sample<<'\n';
                for(const auto point:{source,destination}) {
                    const int index=view.m_terrainEdit.overlayMaterialIndex(point.x(),point.y());
                    const auto material=view.m_mprMaterials.value(index);
                    log<<"WATER MATERIAL cell="<<point.x()<<','<<point.y()<<" index="<<index<<" type="<<material.type<<" opacity="<<material.opacity<<'\n';
                }
                const auto pointer=aim(QVector3D(source.x()*2+1,source.y()*2+1,0));
                for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,pointer,type==QEvent::MouseMove?Qt::NoButton:Qt::RightButton,
                        type==QEvent::MouseButtonPress?Qt::RightButton:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(&view,&event);settle();
                }
                check(view.sampledWaterPaintingEnabled() && view.m_sampledWaterPacked==sample,
                    "RMB retains exact water tile and packed rotation in Modern");
                const int atlasSize=view.m_workingTerrainAtlas->activeTileCount();
                stroke("sampled-water",QVector3D(destination.x()*2+1,destination.y()*2+1,0),false,false);
                check(view.m_terrainEdit.packedTile(destination.x(),destination.y(),ETerrainTileLayer::Overlay)==sample,
                    "LMB transfers exact sampled water tile after release and Redo");
                check(view.m_workingTerrainAtlas->activeTileCount()==atlasSize,"water sample creates no atlas variants");
                check(view.sampledWaterPaintingEnabled(),"water sample survives stroke and Undo Redo");
                stroke("sampled-water-drag",QVector3D(destination.x()*2+1,destination.y()*2+1,0),false,true);
                check(animationFrames()==originalAnimationFrames,"native water animation images preserved");
                if(failed){log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return 1;}
                choose(view.m_modernOverlayPaintChoices,"Lava");
                check(!view.sampledWaterPaintingEnabled(),"palette selection clears physical water sample");
                stroke("water-to-lava",QVector3D(destination.x()*2+1,destination.y()*2+1,0),false,true);
                QObject::connect(&view,&CView::terrainPublicationPreflightRequested,&view,[&](const QString& path,bool* accepted){if(accepted)*accepted=QDir::cleanPath(path)==QDir::cleanPath(root+"/res/HDPack_Terrain.res");});
                QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* b=qobject_cast<QMessageBox*>(w)){log<<"UNEXPECTED "<<b->text()<<'\n';++failed;b->reject();}});dismiss.start(50);
                view.saveLand();
                if(!check(!view.m_pLand->isDirty(),"ordinary Save after sampled water and material brush"))return 1;
                // Save may legitimately compact/remap physical atlas slots.
                // Reopening must reproduce the committed post-save model.
                const auto saved=snapshot(view.m_terrainEdit);
                const auto savedImages=view.m_workingTerrainAtlas->activeTileImages();
                const auto savedPacked=view.m_terrainEdit.packedTile(source.x(),source.y(),ETerrainTileLayer::Overlay);
                const auto savedWater=SMprPackedTile::decode(savedPacked);
                view.unloadLand(false);view.makeCurrent();view.loadLandscape(QFileInfo(map),false);view.updateGL();timer.restart();
                while(timer.elapsed()<120000 && (view.m_terrainAutoCatalogPending || view.m_terrainAutoCatalogBuilding)){settle();QThread::msleep(5);}
                check(snapshot(view.m_terrainEdit)==saved,"reopen preserves water and material stroke bytes");
                const auto reopenedWater=SMprPackedTile::decode(view.m_terrainEdit.packedTile(source.x(),source.y(),ETerrainTileLayer::Overlay));
                const auto waterBefore=savedImages.value(savedWater.atlasSlot());
                const auto waterAfter=view.m_workingTerrainAtlas->activeTileImages().value(reopenedWater.atlasSlot());
                log<<"WATER SAVE before="<<waterBefore.width()<<'x'<<waterBefore.height()<<" format="<<int(waterBefore.format())
                    <<" after="<<waterAfter.width()<<'x'<<waterAfter.height()<<" format="<<int(waterAfter.format())
                    <<" rotations="<<int(savedWater.rotation)<<','<<int(reopenedWater.rotation)<<'\n';
                check(reopenedWater.rotation==savedWater.rotation
                    && waterAfter.convertToFormat(QImage::Format_RGBA8888)==waterBefore.convertToFormat(QImage::Format_RGBA8888),
                    "native water artwork and rotation survive Save/reopen");
                log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
            }
            if(fixture.value("ownerReleaseRegression").toBool()) {
                click("landscapePaintSegment");click("terrainLayerSegment_0");
                click("circlePaintShape");click("vertexPaintTarget");
                radius->setValue(fixture.value("ownerBrushSize").toInt(4));
                if(fixture.value("performanceCamera").toBool()) {
                    aim(QVector3D(55,40,0));
                    QVector<qint64> frames;
                    const QPoint center=view.rect().center();
                    for(int frame=0;frame<40;++frame) {
                        QElapsedTimer elapsed;elapsed.start();
                        QMouseEvent move(QEvent::MouseMove,center+QPoint(frame%20,frame/20),
                                         Qt::NoButton,Qt::MiddleButton,Qt::NoModifier);
                        QApplication::sendEvent(&view,&move);
                        view.updateGL();view.makeCurrent();
                        QOpenGLContext::currentContext()->functions()->glFinish();
                        frames.append(elapsed.nsecsElapsed()/1000);
                        settle();
                    }
                    std::sort(frames.begin(),frames.end());
                    log<<"TIMING cameraFrames=40 medianUs="<<frames[20]
                       <<" p95Us="<<frames[37]<<" maxUs="<<frames.last()<<'\n';
                }
                QStringList materials={"Ground / Common","Yellow lichen","Grass / Burnt"};
                if(fixture.contains("releaseMaterials")){materials.clear();for(const auto& v:fixture["releaseMaterials"].toArray())materials.append(v.toString());}
                QVector<QPoint> points={QPoint(45,33),QPoint(73,50),QPoint(90,70)};
                if(fixture.contains("releasePoints")){points.clear();for(const auto& v:fixture["releasePoints"].toArray()){const auto p=v.toArray();if(!check(p.size()==2,"release point is an XY pair"))return 1;points.append(QPoint(p[0].toInt(),p[1].toInt()));}}
                for(const QString& material:materials) {
                    if(!choose(view.m_modernLandPaintChoices,material))continue;
                    for(const QPoint& point:points) {
                        const QString name=QString("release-%1-%2-%3").arg(panel.paintTerrainType()).arg(point.x()).arg(point.y());
                        stroke(name,QVector3D(point.x(),point.y(),0),true,true);
                        check(view.m_tileStrokeRefused==0,"release accepted "+name);
                        log.flush();
                        if(failed){log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return 1;}
                    }
                }
                log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
            }
            // Owner screenshot: Surface delete while every stored paint option is ON.
            surface(ELandscapeEditMode::OverlaySurface);
            choose(view.m_modernOverlayPaintChoices,"Water / Still");click("squarePaintShape");
            QKeyEvent surfaceC(QEvent::KeyPress,Qt::Key_C,Qt::NoModifier);QApplication::sendEvent(&view,&surfaceC);settle();
            check(panel.paintPaletteMode()==ETerrainPaintPaletteMode::Modern,"unsupported Surface C stays in Modern");
            auto* operation=panel.findChild<QComboBox*>("overlaySurfaceOperation");operation->setCurrentIndex(1);settle();
            check(!panel.modernMixedEdge() && button("modernMixedEdge")->isChecked() && !button("modernMixedEdge")->isEnabled(),"delete ignores Smooth and retains preference");
            const auto activeSurfaces=[&](){int count=0;for(int y=0;y<view.m_terrainEdit.tileCountY();++y)for(int x=0;x<view.m_terrainEdit.tileCountX();++x){bool active=false;view.m_terrainEdit.overlayMaterialIndex(x,y,&active);count+=active;}return count;};
            const int surfacesBefore=activeSurfaces();
            stroke("surface-delete",QVector3D(33,33,0),false,true);
            const int surfacesDeleted=activeSurfaces();check(surfacesDeleted<surfacesBefore,"delete removes active surface topology");
            operation->setCurrentIndex(0);settle();
            choose(view.m_modernOverlayPaintChoices,"Water / Still");
            auto* material=panel.findChild<QComboBox*>("overlaySurfaceMaterial");
            check(material && material->count()>0,"existing surface material records");
            if(material && material->count())material->setCurrentIndex(0);
            stroke("surface-add",QVector3D(33,33,0),false,true);
            check(activeSurfaces()>surfacesDeleted,"add restores active surface topology");
            surface(ELandscapeEditMode::OverlayPaint);
            click("terrainLayerSegment_1");click("squarePaintShape");click("tilePaintTarget");
            check(!panel.modernMixedEdge() && !panel.randomPaintVariants() && !panel.randomizeUniformRotation(),"overlay ignores Ground-only options");
            for(const QString& family:{QString("Lava"),QString("Swamp"),QString("Water / Still")})if(choose(view.m_modernOverlayPaintChoices,family)) {
                stroke("overlay-"+family.section(" / ",0,0).toLower(),QVector3D(33,33,0),false,true);
                if(family!="Lava") {
                    int activations=0;for(int y=12;y<=21;++y)for(int x=12;x<=24;++x) {
                        bool active=false;view.m_terrainEdit.overlayMaterialIndex(x,y,&active);if(!active)continue;
                        const auto packed=view.m_terrainEdit.packedTile(x,y,ETerrainTileLayer::Overlay);
                        const int slot=SMprPackedTile::decode(packed).atlasSlot();bool known=false;
                        const auto signature=view.m_workingTerrainAtlas->catalog().gridSignatureForPacked(packed,&known);
                        for(const auto& animation:view.m_mprAnimatedTiles)if(known && signature.northWest==panel.paintTerrainType()
                            && animation.frameCount>1 && slot==animation.tileIndex)++activations;
                    }
                    check(activations>0,"animated placement uses flipbook start "+family);
                }
            }
            click("vertexPaintTarget");if(choose(view.m_modernOverlayPaintChoices,"Lava"))stroke("overlay-vertex",QVector3D(33,33,0),false,false);
            const auto manual=[&](bool ground){
                click("vertexPaintTarget");QKeyEvent press(QEvent::KeyPress,Qt::Key_C,Qt::NoModifier);QApplication::sendEvent(&view,&press);settle();
                check(panel.manualTileSelectionEnabled() && panel.paintTargetMode()==ETerrainPaintTargetMode::Tile && !panel.modernMixedEdge(),"held C uses physical Tile without Modern options");
                // Select an authored physical tile different from the current one.
                const int slot=0;
                check(panel.selectManualAtlasTile(slot),"manual physical tile selected");
                stroke(ground?"manual-ground":"manual-overlay",QVector3D(37,33,0),ground,true);
                QKeyEvent release(QEvent::KeyRelease,Qt::Key_C,Qt::NoModifier);QApplication::sendEvent(&view,&release);settle();
                check(panel.paintPaletteMode()==ETerrainPaintPaletteMode::Modern && panel.paintTargetMode()==ETerrainPaintTargetMode::Vertex,"release C restores Modern and Vertex preference");
            };
            manual(false);
            if(fixture.value("tracePaintDrag").toBool()){log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;}
            click("landscapePaintSegment");click("terrainLayerSegment_0");click("circlePaintShape");
            if(choose(view.m_modernLandPaintChoices,"Yellow lichen"))stroke("ground-lichen",QVector3D(45,33,0),true,true);
            check(panel.modernMixedEdge() && button("modernMixedEdge")->isEnabled(),"return to static Ground restores Smooth");
            click("tilePaintTarget");click("squarePaintShape");
            if(choose(view.m_modernLandPaintChoices,"Grass"))stroke("ground-tile",QVector3D(49,33,0),true,false);
            manual(true);
            click("circlePaintShape");if(choose(view.m_modernLandPaintChoices,"Yellow lichen"))
                stroke("ground-over-animation",QVector3D(37,33,0),true,true);
            QKeyEvent transitionPress(QEvent::KeyPress,Qt::Key_C,Qt::NoModifier);QApplication::sendEvent(&view,&transitionPress);settle();
            surface(ELandscapeEditMode::OverlaySurface);
            QKeyEvent transitionRelease(QEvent::KeyRelease,Qt::Key_C,Qt::NoModifier);QApplication::sendEvent(&view,&transitionRelease);settle();
            check(panel.paintPaletteMode()==ETerrainPaintPaletteMode::Modern,"C held across a Surface switch cannot leave Manual stuck");
            click("landscapePaintSegment");
            click("terrainLayerSegment_2");click("circlePaintShape");
            check(!panel.modernMixedEdge() && button("modernMixedEdge")->isChecked(),"underwater ignores Smooth and retains preference");
            bool underwater=false;
            for(const auto& c:view.m_modernUnderwaterPaintChoices)if(c.paintAvailable && panel.selectPaintTerrainType(c.id)) {
                log<<"UNDERWATER "<<c.name<<'\n';underwater=stroke("underwater",QVector3D(33,33,0),true,true);if(underwater)break;
            }
            check(underwater,"available Underwater Ground paints");
            surface(ELandscapeEditMode::OverlaySculpt);
            panel.findChild<QDoubleSpinBox*>("landscapeSculptRadius")->setValue(3);
            panel.setFlattenHeight(10000);
            for(int tool=0;tool<6;++tool) {
                click(qPrintable(QString("sculptTool_%1").arg(tool)));
                stroke(QString("surface-sculpt-%1").arg(tool),QVector3D(33,33,0),false,true);
            }
            click("landscapeSculptSegment");
            for(int tool=0;tool<6;++tool) {
                click(qPrintable(QString("sculptTool_%1").arg(tool)));
                stroke(QString("ground-sculpt-%1").arg(tool),QVector3D(49,33,0),true,true);
            }
            click("landscapePaintSegment");click("terrainLayerSegment_0");
            check(button("modernMixedEdge")->isChecked(),"full mode roundtrip retains saved options");
            int prevented=0;
            for(const auto& c:view.m_modernLandPaintChoices)if(!c.paintAvailable) {
                const auto selected=panel.paintTerrainType();
                for(auto* b:panel.findChildren<QToolButton*>())if(b->property("terrainMaterialId").isValid() && b->property("terrainMaterialId").toInt()==c.id) {
                    b->click();check(!b->isEnabled() && !b->toolTip().isEmpty() && panel.paintTerrainType()==selected,"unavailable source click is explained and prevented");++prevented;break;
                }
                break;
            }
            check(prevented>0,"incomplete source case present in configured real pool");
            QVector<qint32> landField,landOriginal,overlayField,overlayOriginal;
            check(animationFrames()==originalAnimationFrames,"all original animation frames preserved by painting");
            view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&landField,&landOriginal);
            view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Overlay,&overlayField,&overlayOriginal);
            QObject::connect(&view,&CView::terrainPublicationPreflightRequested,&view,[&](const QString& path,bool* accepted){if(accepted)*accepted=QDir::cleanPath(path)==QDir::cleanPath(root+"/res/HDPack_Terrain.res");});
            QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* b=qobject_cast<QMessageBox*>(w)){log<<"UNEXPECTED "<<b->text()<<'\n';++failed;b->reject();}});dismiss.start(50);
            view.saveLand();
            if(!check(!view.m_pLand->isDirty(),"ordinary Save of all matrix edits"))return 1;
            const auto saved=snapshot(view.m_terrainEdit);const auto savedFrame=capture();
            view.unloadLand(false);view.makeCurrent();view.loadLandscape(QFileInfo(map),false);view.updateGL();timer.restart();
            while(timer.elapsed()<120000 && (view.m_terrainAutoCatalogPending || view.m_terrainAutoCatalogBuilding)){settle();QThread::msleep(5);}
            check(snapshot(view.m_terrainEdit)==saved,"fresh reopen exact sectors after mixed-mode Save");
            QVector<qint32> fresh,original;
            check(view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&fresh,&original) && fresh==landField,"fresh Ground semantic identities");
            check(view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Overlay,&fresh,&original) && fresh==overlayField,"fresh Overlay semantic identities");
            check(view.m_terrainAtlasAnalysis.usable && !view.m_modernUnderwaterPaintChoices.isEmpty(),"fresh reopen retains a usable authoring palette");
            const auto reopenedAnimationFrames=animationFrames();bool animationsRetained=reopenedAnimationFrames.size()==originalAnimationFrames.size();
            for(int sequence=0;sequence<reopenedAnimationFrames.size() && animationsRetained;++sequence) {
                const auto& actual=reopenedAnimationFrames.at(sequence);const auto& originalFrames=originalAnimationFrames.at(sequence);
                animationsRetained=actual.size()==originalFrames.size() && !actual.isEmpty();if(!animationsRetained)break;
                // Normal publication uses one common page size and DXT1. Test
                // retained frame order/content against that codec conversion,
                // rather than calling resampled/recompressed frames byte-identical.
                const int tileSize=actual.first().width();
                log<<"ANIMATION sequence="<<sequence<<" frames="<<actual.size()<<" originalSize="<<originalFrames.first().width()<<" publishedSize="<<tileSize<<'\n';
                const auto expectedPage=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(originalFrames,0,tileSize),true);
                for(int frame=0;frame<actual.size();++frame){QImage expected;animationsRetained &= ei::decodeMmpDxt1AtlasTile(expectedPage,frame,&expected)
                    && expected.convertToFormat(QImage::Format_RGBA8888)==actual.at(frame).convertToFormat(QImage::Format_RGBA8888);}
            }
            check(animationsRetained,"fresh reopen retains frame order/content through normal page/DXT1 conversion");
            choose(view.m_modernLandPaintChoices,"Yellow lichen");
            stroke("reopen-ground",QVector3D(55,33,0),true,true);
            aim(QVector3D(33,33,0));capture().save(output+"/reopen.png");savedFrame.save(output+"/saved.png");dismiss.stop();
            timer.restart();while(timer.elapsed()<120000 && (view.m_terrainGlobalHydrationPendingJobs>0 || view.m_terrainGlobalHydrationPublishQueued)){settle();QThread::msleep(5);}
            check(view.m_terrainGlobalHydrationPendingJobs==0 && !view.m_terrainGlobalHydrationPublishQueued,"fresh automatic thumbnails settle");
            panel.resize(760,1400);timer.restart();while(timer.elapsed()<300){settle();QThread::msleep(5);}panel.grab().save(output+"/matrix-panel.png");
            log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
        }
        if(fixture.value("ownerPaintRegression").toBool()) {
            panel.activateMode(ELandscapeEditMode::TilePaint,true);
            qint32 material=-1;
            for(const auto& family:view.m_terrainGlobalFamilies)
                if(family.layer==ETerrainAtlasLayer::Land && family.name.contains("Yellow lichen"))material=family.id;
            if(!check(material>=0 && panel.selectPaintTerrainType(material),"owner Yellow lichen material selected"))return 1;
            for(const char* name:{"modernAutoResolveBlends","modernMixedEdge","randomPaintVariants","generatedHdVariants","vertexPaintTarget","circlePaintShape"}) {
                auto* option=panel.findChild<QAbstractButton*>(name);
                if(!check(option!=nullptr,QString("owner option %1").arg(name)))return 1;
                option->setChecked(true);
            }
            auto* radius=panel.findChild<QSlider*>("terrainPaintRadius");
            if(!check(radius!=nullptr,"owner brush size control"))return 1;
            radius->setValue(3);
            QObject::connect(&view,&CView::updateMsg,[&](const QString& message){log<<"MESSAGE "<<message<<'\n';log.flush();});
            QVector3D world(33,33,0);view.m_pLand->projectPt(world,true);
            view.moveCameraToPoint(world);view.m_cam->moveAwayOn(35);view.update();
            view.updateGL();
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            const QVector3D projected=world.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));
            const QPoint pointer(qRound(projected.x()),qRound(view.height()-projected.y()));
            if(!check(view.rect().contains(pointer) && projected.z()>=0 && projected.z()<=1,"owner world point projects inside viewport"))return 1;
            bool picked=false;const QVector3D hit=view.getTerrainBrushPos(pointer.x(),pointer.y(),&picked,true);
            log<<"OWNER PICK requested="<<world.x()<<','<<world.y()<<','<<world.z()<<" actual="<<hit.x()<<','<<hit.y()<<','<<hit.z()<<'\n';
            if(!check(picked && (hit-world).length()<1,"owner mouse ray hits requested terrain point"))return 1;
            const auto captureViewport=[&](){
                view.makeCurrent();
                QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
                QOpenGLFramebufferObject target(view.size(),format);
                if(!target.isValid() || !target.bind())return QImage();
                auto* gl=QOpenGLContext::currentContext()->functions();
                gl->glViewport(0,0,view.width(),view.height());gl->glEnable(GL_DEPTH_TEST);gl->glDepthMask(GL_TRUE);
                gl->glClearColor(0,0,0,1);gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                const QMatrix4x4 previousProjection=view.m_projection;
                view.m_projection.setToIdentity();view.m_projection.ortho(-9,9,-7,7,1,100);
                view.m_minimapCaptureActive=true;view.m_minimapCaptureUnlitPass=true;
                view.m_minimapCaptureCameraPosition=world+QVector3D(0,0,50);
                view.m_minimapCaptureView.setToIdentity();
                view.m_minimapCaptureView.lookAt(view.m_minimapCaptureCameraPosition,world,QVector3D(0,1,0));
                const bool previousWater=view.m_bDrawWater;view.m_bDrawWater=false;
                view.draw();const QImage frame=target.toImage();
                log<<"OWNER GL landProgram="<<view.m_landProgram.isLinked()<<" error="<<gl->glGetError()<<'\n';
                view.m_bDrawWater=previousWater;view.m_projection=previousProjection;
                view.m_minimapCaptureActive=false;view.m_minimapCaptureUnlitPass=false;
                target.release();return frame;
            };
            const auto frameBefore=captureViewport();frameBefore.save(output+"/owner-paint-before.png");
            const QRect pixelRegion(qMax(0,qRound(hit.x()/2)-4),qMax(0,qRound(hit.y()/2)-4),9,9);
            preview(view.m_terrainEdit,view.m_workingTerrainAtlas->activeTileImages(),pixelRegion).save(output+"/owner-pixels-before.png");
            const auto baseline=snapshot(view.m_terrainEdit);
            const int undoBefore=history.count();
            for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,pointer,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(&view,&event);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            const auto painted=snapshot(view.m_terrainEdit);
            const auto frameAfter=captureViewport();frameAfter.save(output+"/owner-paint-after.png");
            preview(view.m_terrainEdit,view.m_workingTerrainAtlas->activeTileImages(),pixelRegion).save(output+"/owner-pixels-after.png");
            check(!frameBefore.isNull() && !frameAfter.isNull() && frameBefore!=frameAfter,
                  "owner native offscreen viewport presents changed terrain pixels");
            check(painted!=baseline,"owner mouse route changes Yellow lichen terrain");
            check(history.count()==undoBefore+1,"owner mouse stroke is one undo transaction");
            history.undo();check(snapshot(view.m_terrainEdit)==baseline,"owner mouse stroke Undo exact");
            history.redo();check(snapshot(view.m_terrainEdit)==painted,"owner mouse stroke Redo exact");
            for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,pointer,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(&view,&event);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            check(view.m_tileStrokeRefused==0,"owner repeated mouse stroke does not refuse a nonresident mixed tuple");
            const auto dragBefore=snapshot(view.m_terrainEdit);const int dragUndoBefore=history.count();
            const QPoint dragEnd=pointer+QPoint(25,0);
            for(int step=0;step<4;++step) {
                const auto type=step==0?QEvent::MouseMove:step==1?QEvent::MouseButtonPress:step==2?QEvent::MouseMove:QEvent::MouseButtonRelease;
                const QPoint position=step<2?pointer:dragEnd;
                QMouseEvent event(type,position,(step==1||step==3)?Qt::LeftButton:Qt::NoButton,
                    (step==1||step==2)?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(&view,&event);QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            const auto dragged=snapshot(view.m_terrainEdit);
            check(view.m_tileStrokeRefused==0 && dragged!=dragBefore && history.count()==dragUndoBefore+1,
                  "owner mouse drag aggregates stamps in one exact transaction");
            history.undo();check(snapshot(view.m_terrainEdit)==dragBefore,"owner mouse drag Undo exact");
            history.redo();check(snapshot(view.m_terrainEdit)==dragged,"owner mouse drag Redo exact");
            panel.resize(760,1400);
            QElapsedTimer panelLayout;panelLayout.start();
            while(panelLayout.elapsed()<300) {QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,50);QThread::msleep(5);}
            if(auto* chips=panel.findChild<QGridLayout*>("terrainPaintChipGrid")) {
                int row=0,column=0,rowSpan=0,columnSpan=0;chips->getItemPosition(1,&row,&column,&rowSpan,&columnSpan);
                check(column==1,"readable owner-width panel keeps two option columns");
            }
            panel.grab().save(output+"/owner-panel.png");
            panel.activateMode(ELandscapeEditMode::Sculpt,true);QApplication::processEvents();
            panel.grab().save(output+"/owner-relief-panel.png");
            panel.activateMode(ELandscapeEditMode::TilePaint,true);QApplication::processEvents();
            view.updateGL();view.grabFrameBuffer().save(output+"/owner-paint.png");
            if(fixture.value("ownerSaveRegression").toBool()) {
                if(!check(root.contains("/build-tests-154-owner-save/fixture"),"ordinary Save writes only the owned private fixture"))return 1;
                QVector<qint32> expectedField,originalField;
                check(view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&expectedField,&originalField),
                      "mouse stroke owns a complete semantic field before Save");
                const auto mapBefore=QCryptographicHash::hash(read(map),QCryptographicHash::Sha256);
                QObject::connect(&view,&CView::terrainPublicationPreflightRequested,&view,[&](const QString& path,bool* accepted){
                    if(accepted)*accepted=QDir::cleanPath(path)==QDir::cleanPath(root+"/res/HDPack_Terrain.res");
                });
                QTimer refuseUnexpectedDialog;
                QObject::connect(&refuseUnexpectedDialog,&QTimer::timeout,[&](){
                    for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget)) {
                        log<<"Unexpected ordinary Save dialog: "<<box->text()<<'\n';++failed;box->reject();
                    }
                });refuseUnexpectedDialog.start(50);
                QElapsedTimer saveTimer;saveTimer.start();view.saveLand();
                log<<"TIMING ordinarySaveMs="<<saveTimer.elapsed()<<'\n';
                if(!check(!view.m_pLand->isDirty()
                    && QCryptographicHash::hash(read(map),QCryptographicHash::Sha256)!=mapBefore,
                    "ordinary Save MPR publishes and adopts the copied pair"))return 1;
                const auto savedSectors=snapshot(view.m_terrainEdit);
                view.unloadLand(false);view.makeCurrent();view.loadLandscape(QFileInfo(map),false);view.updateGL();
                QElapsedTimer reload;reload.start();
                while(reload.elapsed()<120000 && (view.m_terrainAutoCatalogPending || view.m_terrainAutoCatalogBuilding)) {
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,50);QThread::msleep(5);
                }
                check(snapshot(view.m_terrainEdit)==savedSectors,"ordinary Save reopen retains exact committed sectors");
                QVector<qint32> reopenedField,reopenedBaseline;
                check(view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&reopenedField,&reopenedBaseline)
                    && reopenedField==expectedField,"ordinary Save reopen retains exact mouse-painted material identities");
                refuseUnexpectedDialog.stop();
            }
            log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
        }
        if(fixture.value("panelOnly").toBool()) {
            int families=0,available=0;
            for(const auto& choice:view.m_modernLandPaintChoices) {
                int sources=0;for(const auto& image:choice.authoredThumbnails)sources+=!image.isNull();
                if(sources>1)++families;available+=sources;
                check(choice.authoredThumbnails.size()==choice.assignedSlots,"authored strip retains exact source count");
            }
            log<<"ORIGINALS multiSourceFamilies="<<families<<" available="<<available<<'\n';
            check(families>0,"real-map palette supplies several verified originals per material");
        }
        panel.activateMode(ELandscapeEditMode::TilePaint,true);panel.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
        int land=0,overlay=0;
        for(const auto& family:view.m_terrainGlobalFamilies) {
            if(family.layer==ETerrainAtlasLayer::Land && panel.selectPaintTerrainType(family.id))++land;
        }
        panel.grab().save(output+"/palette-land.png");
        panel.activateMode(ELandscapeEditMode::OverlayPaint,true);
        for(const auto& family:view.m_terrainGlobalFamilies) {
            if(family.layer==ETerrainAtlasLayer::Overlay && panel.selectPaintTerrainType(family.id))++overlay;
        }
        panel.grab().save(output+"/palette-overlay.png");
        check(land>0 && overlay>0,QString("selectable land=%1 overlay=%2").arg(land).arg(overlay));
        if(fixture.value("panelOnly").toBool()) {
            for(int width : {360,420}) {
                panel.resize(width,1000);
                for(auto mode : {ELandscapeEditMode::TilePaint,ELandscapeEditMode::Sculpt,ELandscapeEditMode::OverlaySurface}) {
                    panel.activateMode(mode,true);
                    QElapsedTimer layout;layout.start();
                    while(layout.elapsed()<300) {
                        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,50);QThread::msleep(5);
                    }
                    if(mode==ELandscapeEditMode::TilePaint) {
                        auto* scroll=panel.findChild<QScrollArea*>("groundMaterialScroll");
                        int firstY=INT_MAX,columns=0;
                        if(scroll)for(auto* button:scroll->findChildren<QToolButton*>())
                            if(button->property("terrainMaterialId").isValid() && button->isVisible())firstY=qMin(firstY,button->y());
                        if(scroll)for(auto* button:scroll->findChildren<QToolButton*>())
                            if(button->property("terrainMaterialId").isValid() && button->isVisible() && button->y()==firstY)++columns;
                        log<<"LAYOUT width="<<width<<" firstMaterialRow="<<columns<<" fontPx="<<panel.font().pixelSize()<<'\n';
                        check(columns==4,"default real-map palette has four tiles per row");
                    }
                    check(panel.grab().save(output+QString("/panel-%1-%2.png").arg(width).arg(int(mode))),"real-map panel snapshot");
                }
            }
            log<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
            return failed?1:0;
        }
        panel.activateMode(ELandscapeEditMode::TilePaint,true);
        if(auto* option=panel.findChild<QAbstractButton*>("modernMixedEdge"))option->setChecked(false);
        const auto before=snapshot(view.m_terrainEdit);
        bool changed=false;
        for(const auto& family:view.m_terrainGlobalFamilies) {
            if(family.layer!=ETerrainAtlasLayer::Land || !panel.selectPaintTerrainType(family.id))continue;
            QVector3D point(33,33,0);view.m_pLand->projectPt(point,true);
            view.updatePreviewTile(point,true);view.beginTileBrushGroup();view.setTile(point,true);view.endTileBrushGroup();
            if(snapshot(view.m_terrainEdit)!=before){changed=true;break;}
        }
        if(!check(changed,"production land brush changes real copied map"))return 1;
        const auto painted=snapshot(view.m_terrainEdit);history.undo();check(snapshot(view.m_terrainEdit)==before,"stroke Undo");
        history.redo();check(snapshot(view.m_terrainEdit)==painted,"stroke Redo");
        QObject::connect(&view,&CView::terrainPublicationPreflightRequested,&view,[](const QString&,bool* accepted){if(accepted)*accepted=true;});
        QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[&](){
            for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget)) {
                if(box->text().startsWith(QStringLiteral("Saving this draft will replace atlas/profile entries"))
                   && box->standardButtons().testFlag(QMessageBox::Yes)) {
                    log<<"Accept copied-HDPack draft replacement\n";
                    box->button(QMessageBox::Yes)->click();continue;
                }
                log<<"Unexpected save dialog: "<<box->text()<<'\n';++failed;box->reject();
            }
        });dismiss.start(50);
        const bool saved=view.saveChangedLandscapeDocument(QFileInfo(map),true,true);
        check(saved,"save through production writer on private copied map");
        CMprDocument reopened;CTerrainEditModel model;CValidationReport report;
        check(saved && reopened.load(QFileInfo(map),&report) && model.load(reopened,&report)
              && !report.hasErrors() && snapshot(model)==painted,"reopen copied MPR with exact painted sectors");
        log<<report.toText()<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
        return failed?1:0;
    }
    QSettings().remove("ui/landscape/randomPaintVariants");
    {
        CLandscapeTools panel;
        check(panel.randomPaintVariants(),"random painting defaults ON");
        check(!panel.randomizeUniformRotation(),"new rotation policy suppresses legacy uniform rotation");
        auto option=panel.findChild<QCheckBox*>("randomPaintVariants");
        auto shuffle=panel.findChild<QPushButton*>("shuffleTerrainTiles");
        auto scope=panel.findChild<QComboBox*>("randomPaintScope");
        check(option && shuffle && !panel.findChild<QWidget*>("randomPaintSeed")
            && !panel.findChild<QWidget*>("newRandomPaintSeed") && scope && scope->count()==2,
            "panel exposes shuffle scopes without a seed editor");
        bool emitted=false;
        QObject::connect(&panel,&CLandscapeTools::shuffleTerrainTilesRequested,
            [&](bool selection){emitted=!selection;});
        shuffle->click();check(emitted,"shuffle signal carries scope; view creates its own seed");
        option->setChecked(false);CLandscapeTools restored;
        check(!restored.randomPaintVariants(),"OFF preference persists");
        check(restored.randomizeUniformRotation(),"OFF retains legacy uniform rotation switch");
    }
    CValidationReport report;CMprDocument document;
    if(!check(document.loadBytes(syntheticMpr(),output+"/synthetic.mpr",&report),"synthetic map with overlay and flipbook loads")) return 1;
    CTerrainEditModel model;
    if(!check(model.load(document,&report),"synthetic edit model")) return 1;
    STerrainAtlasProfile profile;profile.mprNamespace="Random";profile.atlasSlotCount=64;
    profile.textureCount=1;profile.atlasPixelSha256=QByteArray(64,'a');profile.approved=true;
    for(int id=1;id<=3;++id) {
        STerrainAtlasProfileFamily f;f.id=id;f.name=id==1?"Grass":id==2?"Boards":"Water";
        f.randomRotationAllowed=id==1;f.randomRotationExplicit=true;f.tileType=id;profile.families.append(f);
    }
    auto add=[&](int slot,const STerrainTransitionSignature& s,int weight=1,bool mixed=false) {
        STerrainAtlasProfileSlot def;def.slot=slot;def.signature=s;def.distributionWeight=weight;
        if(mixed)def.mixedEdgeRecipes.append("mixed-edge-b2:"+QString(64,'a'));
        profile.slotDefinitions.append(def);
    };
    add(0,signature(1,1,1,1));add(1,signature(1,1,1,1),3);add(2,signature(1,1,1,1));
    add(3,signature(2,2,2,2));add(4,signature(1,1,1,1),1,true);
    add(5,signature(1,1,1,1));add(6,signature(1,1,1,1));
    add(7,signature(1,1,1,2));add(8,signature(1,1,1,2));
    add(9,signature(1,2,1,2));add(10,signature(1,2,1,2));add(11,signature(3,3,3,3));
    for(int slot=12;slot<64;++slot) {
        STerrainAtlasProfileSlot def;def.slot=slot;def.signature=signature(1,1,1,1);def.assigned=false;
        profile.slotDefinitions.append(def);
    }
    profile.normalizeMembership();
    const auto catalog=profile.usableAnalysis(&report).landCatalog;
    check(catalog.size()>0,"reviewed catalog");
    {
        CTerrainEditModel selfPaint=model;CValidationReport selfReport;
        QVector<qint32> field(17*17,1);field[9*17+9]=3;
        check(selfPaint.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,field,field,&selfReport),
              "self-paint baseline with a nonresident neighbouring tuple");
        STerrainTileBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=0;
        const auto command=selfPaint.makeVirtualMaterialPaint(8,8,brush,ETerrainTileLayer::Land,1,catalog,0,&selfReport,false);
        bool mixedPreserved=true;
        for(const auto& delta:command.deltas)if(delta.globalX==8 && delta.globalY==8)mixedPreserved=false;
        check(!selfReport.hasErrors() && command.virtualVertices.isEmpty() && mixedPreserved,
              "self-paint keeps nonresident mixed artwork without false uniform-source refusal");
        if(selfReport.hasErrors())log<<selfReport.toText();
        QImage budgetImage(32,32,QImage::Format_RGB32);budgetImage.fill(Qt::gray);
        QVector<QImage> images(64,budgetImage);
        CTerrainWorkingAtlas budgetAtlas(catalog,images,0,128,{}, {},document.mapInfo().tileTypes,{{1,1},{2,2},{3,3}});
        CValidationReport budgetReport;
        const auto refused=CTerrainMixedEdge::resolve(selfPaint,&budgetAtlas,{8*16+8},{{1,1},{2,2},{3,3}},
            {},QPoint(),999,0,false,&budgetReport,63);
        bool bankUnavailable=false;
        for(const auto& finding:budgetReport.findings())bankUnavailable|=finding.code=="terrain.mixed_edge.bank_unavailable";
        check(budgetAtlas.isValid() && budgetAtlas.activeTileCount()>63
              && refused.isEmpty() && budgetReport.hasErrors() && !bankUnavailable
              && budgetReport.toText().contains("atlas budget exceeded"),
              "Mixed-edge budget refusal cannot be classified as missing stock for exact fallback");
    }
    STerrainAtlasProfile loaded;
    check(STerrainAtlasProfile::fromBytes(profile.toBytes(),&loaded,&report)
        && loaded.families[0].randomRotationAllowed && !loaded.families[1].randomRotationAllowed
        && loaded.slotDefinitions[1].distributionWeight==3,"profile flag and weight roundtrip");
    if(report.hasErrors()) log << report.toText();
    {
        QVector<QImage> images(64,QImage(32,32,QImage::Format_RGB32));
        CTerrainAtlasReviewDialog dialog(profile,images);
        auto flag=dialog.findChild<QCheckBox*>("materialRandomRotation");
        check(flag!=nullptr,"Material Atlas rotation flag");
        auto tree=dialog.findChild<QTreeWidget*>();
        if(tree && tree->topLevelItemCount() && tree->topLevelItem(0)->childCount()) {
            auto item=tree->topLevelItem(0)->child(0);tree->setCurrentItem(item);item->setSelected(true);
            QMetaObject::invokeMethod(&dialog,"selectTreeItem",Qt::DirectConnection,Q_ARG(QTreeWidgetItem*,item));
            flag->setChecked(false);
            check(!dialog.reviewedProfile().families[0].randomRotationAllowed
                && dialog.reviewedProfile().families[0].randomRotationExplicit,"flag edits material profile");
        } else check(false,"material tree selection");
    }
    const QRect all(0,0,16,16);const QSet<int> excluded{5,6};
    const QByteArray before=snapshot(model);
    const int catalogEntries=catalog.size();
    auto command=model.makeRandomTilePaint(catalog,123,all,excluded);
    const auto repeated=model.makeRandomTilePaint(catalog,123,all,excluded);
    check(command.deltas.size()==repeated.deltas.size() && !command.deltas.isEmpty(),"deterministic command count");
    bool exact=true;for(int i=0;i<command.deltas.size();++i)exact&=command.deltas[i].after==repeated.deltas[i].after;
    check(exact,"deterministic packed choices");
    command.changesRandomSeed=true;command.beforeRandomSeed=model.randomPaintSeed();command.afterRandomSeed=123;
    QUndoStack undo;auto history=new RandomCommand(&model,command);undo.push(history);
    check(history->valid && undo.count()==1 && model.randomPaintSeed()==123,"one Undo transaction includes seed");
    const QByteArray after=snapshot(model);
    check(model.makeRandomTilePaint(catalog,123,all,excluded).isEmpty(),"same seed is idempotent after painting");
    QMap<int,int> counts,rotations;bool classes=true,signatures=true,heights=true,overlays=true;
    CTerrainEditModel baseline;baseline.load(document);
    for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
        const auto p=SMprPackedTile::decode(model.packedTile(x,y,ETerrainTileLayer::Land));
        ++counts[p.atlasSlot()];++rotations[p.rotation];
        classes&=model.tileType(p.encode())==baseline.tileType(baseline.packedTile(x,y,ETerrainTileLayer::Land));
        signatures&=catalog.gridSignatureForPacked(p.encode())==catalog.gridSignatureForPacked(baseline.packedTile(x,y,ETerrainTileLayer::Land));
        overlays&=model.packedTile(x,y,ETerrainTileLayer::Overlay)==baseline.packedTile(x,y,ETerrainTileLayer::Overlay);
    }
    for(int y=0;y<33;++y)for(int x=0;x<33;++x)heights&=model.height(x,y)==baseline.height(x,y);
    check(counts.size()==2 && counts.value(1)>150 && counts.value(1)<225,"weighted physical variants 1:3");
    check(rotations.size()==4,"all four rotations distributed");
    check(classes && signatures,"classes and exact signatures unchanged");check(heights && overlays,"heights and overlay unchanged");
    check(catalog.size()==catalogEntries,"no new atlas slots or catalog entries");
    undo.undo();check(history->valid && snapshot(model)==before && !model.hasRandomPaintSeed(),"Undo restores bytes and unset seed");
    undo.redo();check(history->valid && snapshot(model)==after && model.randomPaintSeed()==123,"Redo restores bytes and seed");
    check(!model.makeRandomTilePaint(catalog,124,all,excluded).isEmpty(),"new seed changes cells");
    auto selection=model.makeRandomTilePaint(catalog,124,QRect(3,4,5,6),excluded);
    bool scoped=true;for(const auto& d:selection.deltas)scoped&=QRect(3,4,5,6).contains(d.globalX,d.globalY);
    check(scoped && !selection.deltas.isEmpty(),"selection limits every changed cell");
    for(int slot:{3,4,5,6,7,9,11}) {
        CTerrainEditModel cellModel;cellModel.load(document);
        STerrainTileBrush brush;brush.radius=0;
        const auto initial=cellModel.makeTilePaint(brush,ETerrainTileLayer::Land,0,quint8(slot),slot==3?2:0);
        cellModel.apply(initial,true);
        const auto random=cellModel.makeRandomTilePaint(catalog,789,QRect(0,0,1,1),excluded);
        if(slot==3 || slot==4 || slot==5 || slot==6 || slot==11)
            check(random.isEmpty(),QString("protected board/mixed/flipbook/water slot %1").arg(slot));
        else {
            check(random.isEmpty(),QString("transition physical slot and rotation unchanged %1").arg(slot));
        }
    }
    // Even a symmetric non-directional transition may use incompatible edge
    // artwork. The new pass must preserve both the physical slot and rotation.
    CTerrainTransitionCatalog symmetric;
    for(int slot:{9,10})for(int rotation=0;rotation<4;++rotation) {
        STerrainTransitionVariant v;v.signature=CTerrainTransitionCatalog::rotated(signature(1,2,1,2),-rotation);
        v.packedTile=packed(slot,rotation);symmetric.addVariant(v);
    }
    CTerrainEditModel symmetricModel;symmetricModel.load(document);STerrainTileBrush brush;brush.radius=20;brush.shape=ETerrainTileBrushShape::Square;
    symmetricModel.apply(symmetricModel.makeTilePaint(brush,ETerrainTileLayer::Land,0,9,0),true);
    auto symmetricRandom=symmetricModel.makeRandomTilePaint(symmetric,456,all);
    symmetricModel.apply(symmetricRandom,true);QSet<int> symmetricTurns,symmetricSlots;
    for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
        auto p=SMprPackedTile::decode(symmetricModel.packedTile(x,y,ETerrainTileLayer::Land));symmetricTurns.insert(p.rotation);symmetricSlots.insert(p.atlasSlot());
    }
    check(symmetricRandom.isEmpty() && symmetricTurns==QSet<int>({0}) && symmetricSlots==QSet<int>({9}),
        "symmetric transition variants and rotations remain unchanged");
    // Zero is a valid explicitly chosen seed and survives the MPR member.
    STerrainSemanticFieldDocument semantics;semantics.mprNamespace="Random";semantics.vertexWidth=17;semantics.vertexHeight=17;
    semantics.terrainTilesSha256=STerrainSemanticFieldDocument::tileBindingSha256(document);
    semantics.atlasPixelSha256=QByteArray(32,'a');semantics.profileSha256=QByteArray(32,'b');semantics.hasRandomPaintSeed=true;
    STerrainSemanticFieldDocument readSemantics;
    check(STerrainSemanticFieldDocument::fromBytes(semantics.toBytes(),&readSemantics,&report)
        && readSemantics.hasRandomPaintSeed && readSemantics.randomPaintSeed==0,"explicit zero seed serializes");

    {
        const QString scenePath=output+"/mapper-source.mpr";
        check(write(scenePath,document.originalBytes()),"private mapper fixture");
        CSettings settings;QTableWidget properties;properties.setColumnCount(2);
        QUndoStack history;CProgressView progress;QLineEdit coordinates;CTreeView tree;CTileForm tiles;
        CLandscapeTools panel;panel.findChild<QCheckBox*>("randomPaintVariants")->setChecked(true);
        QVector<QImage> images;for(int i=0;i<64;++i){QImage image(32,32,QImage::Format_RGB32);image.fill(QColor(40+i*2,80+i,30));images.append(image);}
        CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
        view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
        view.attachLandscapeTools(&panel);view.resize(320,240);view.show();QApplication::processEvents();view.makeCurrent();
        view.m_pLand->readMap(QFileInfo(scenePath),false);view.m_mprDocument=document;
        view.m_terrainEdit=baseline;view.m_terrainEditAvailable=true;view.m_terrainAutoCatalogPending=false;
        view.m_terrainSemanticSeed=123;view.m_terrainAtlasProfile=profile;
        view.m_terrainAtlasAnalysis=profile.usableAnalysis();
        view.m_pLand->setCanonicalTerrainModel(&view.m_terrainEdit);
        view.m_authoredTerrainTileImages=images;
        view.m_workingTerrainAtlas.reset(new CTerrainWorkingAtlas(catalog,images,123,4096,{}, {},document.mapInfo().tileTypes,{{1,1},{2,3},{3,1}}));
        view.m_mprAnimatedTiles=document.mapInfo().animatedTiles;tiles.adoptTileImages(images,document.mapInfo().tileTypes);
        auto* shuffleButton=panel.findChild<QPushButton*>("shuffleTerrainTiles");
        shuffleButton->click();
        const quint32 firstSeed=view.m_terrainEdit.randomPaintSeed();
        const auto firstShuffle=snapshot(view.m_terrainEdit);
        check(history.count()==1 && view.m_terrainEdit.hasRandomPaintSeed()
            && firstSeed!=123,"first shuffle stores a fresh map seed in one Undo entry");
        check(firstShuffle!=before,"first shuffle changes typical-map cells");
        shuffleButton->click();
        const quint32 secondSeed=view.m_terrainEdit.randomPaintSeed();
        const auto secondShuffle=snapshot(view.m_terrainEdit);
        check(history.count()==2 && secondSeed!=firstSeed,
            "two consecutive shuffle clicks choose different seeds");
        check(secondShuffle!=firstShuffle,
            "two consecutive shuffle clicks produce different cell layouts");
        history.undo();check(snapshot(view.m_terrainEdit)==firstShuffle
            && view.m_terrainEdit.randomPaintSeed()==firstSeed,
            "second shuffle Undo restores exact prior cells and seed");
        history.undo();check(snapshot(view.m_terrainEdit)==before
            && !view.m_terrainEdit.hasRandomPaintSeed(),
            "first shuffle Undo restores original cells and seed state");
        history.redo();history.redo();
        check(snapshot(view.m_terrainEdit)==secondShuffle
            && view.m_terrainEdit.randomPaintSeed()==secondSeed,
            "Redo replays both shuffle transactions exactly");
        QJsonObject seedMember;SResArchiveNewMember member;QByteArray withSemantics;
        check(view.buildTerrainSemanticFieldMember(document.originalBytes(),QFileInfo(scenePath),"Random",
            profile.atlasPixelSha256,profile.toBytes(),&member,&report,nullptr,&withSemantics)
            && !withSemantics.isEmpty(),"mapper ordinary save embeds explicit seed without requiring semantic field");
        CMprDocument seeded;STerrainSemanticFieldDocument savedSeed;
        check(seeded.loadBytes(withSemantics,"seeded.mpr",&report)
            && STerrainSemanticFieldDocument::fromBytes(seeded.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&savedSeed,&report)
            && savedSeed.hasRandomPaintSeed && savedSeed.randomPaintSeed==secondSeed,
            "mapper saved MPR reopens latest shuffle seed");
        history.clear();view.m_terrainEdit=baseline;
        view.m_tileStrokeLayer=ETerrainTileLayer::Land;
        view.m_tileStrokePaletteMode=int(ETerrainPaintPaletteMode::ClassicAuto);
        STerrainTileBrush animatedBrush;animatedBrush.radius=0;
        for(auto layer : {ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
            view.m_tileStrokeLayer=layer;
            auto transition=view.m_terrainEdit.makeTilePaint(animatedBrush,layer,0,9,2);
            const auto original=transition.deltas;
            view.randomizeTerrainPaintCommand(&transition,catalog);
            bool exact=original.size()==transition.deltas.size();
            for(int i=0;i<original.size() && exact;++i)exact &= original[i].after==transition.deltas[i].after;
            check(exact && !original.isEmpty(),QString("mapper keeps legacy transition choice layer%1").arg(int(layer)));
        }
        view.m_tileStrokeLayer=ETerrainTileLayer::Land;
        auto animatedPaint=view.m_terrainEdit.makeTilePaint(animatedBrush,ETerrainTileLayer::Land,0,5,3);
        view.randomizeTerrainPaintCommand(&animatedPaint,catalog);
        check(!animatedPaint.deltas.isEmpty()
            && SMprPackedTile::decode(animatedPaint.deltas.first().after).atlasSlot()==5
            && SMprPackedTile::decode(animatedPaint.deltas.first().after).rotation==0,
            "Classic animated painting uses fixed rotation and frame");
        view.m_tileStrokeLayer=ETerrainTileLayer::Overlay;
        auto overlayPaint=view.m_terrainEdit.makeTilePaint(animatedBrush,ETerrainTileLayer::Overlay,0,1,3);
        view.randomizeTerrainPaintCommand(&overlayPaint,catalog);
        check(!overlayPaint.deltas.isEmpty()
            && SMprPackedTile::decode(overlayPaint.deltas.first().after).atlasSlot()==1
            && SMprPackedTile::decode(overlayPaint.deltas.first().after).rotation==0,
            "Classic overlay painting uses fixed rotation without rerolling slot");
        view.m_tileStrokeLayer=ETerrainTileLayer::Land;
        // All four production paint builders flow through the same cosmetic
        // command adapter. Compare actual command bytes after replay twice.
        for(int mode=0;mode<2;++mode)for(int target=0;target<2;++target) {
            panel.setPaintPaletteMode(mode?ETerrainPaintPaletteMode::ClassicAuto:ETerrainPaintPaletteMode::Modern);
            view.beginTileBrushGroup();const quint32 firstSeed=view.m_tileStrokeDistributionSeed;
            const quint32 stableSeed=view.terrainRandomPaintSeed();
            const auto paintCatalog=view.m_workingTerrainAtlas->catalog();
            view.m_tileStrokeTargetMode=target;
            STerrainTileBrush paint;paint.centerX=7.5f;paint.centerY=7.5f;paint.radius=20;paint.shape=ETerrainTileBrushShape::Square;
            CValidationReport paintReport;STerrainTileCommand painted;
            if(mode) painted=target?view.m_terrainEdit.makeSemanticVertexPaint(8,8,paint,ETerrainTileLayer::Land,1,paintCatalog,firstSeed,&paintReport)
                :view.m_terrainEdit.makeSemanticTilePaint(paint,ETerrainTileLayer::Land,1,paintCatalog,firstSeed,&paintReport);
            else painted=target?view.m_terrainEdit.makeVirtualMaterialPaint(8,8,paint,ETerrainTileLayer::Land,1,paintCatalog,firstSeed,&paintReport)
                :view.m_terrainEdit.makeVirtualTileMaterialPaint(paint,ETerrainTileLayer::Land,1,paintCatalog,firstSeed,&paintReport);
            view.randomizeTerrainPaintCommand(&painted,catalog);
            check(view.applyTerrainTilePreview(painted,true),QString("mapper paint builder mode%1 target%2 applies").arg(mode).arg(target));
            appendTerrainTileCommand(&view.m_tileStrokeCommand,painted);view.endTileBrushGroup();
            const auto once=snapshot(view.m_terrainEdit);history.clear();
            view.beginTileBrushGroup();view.m_terrainEdit=baseline;
            const quint32 secondSeed=view.m_tileStrokeDistributionSeed;
            STerrainTileCommand repeated;
            if(mode) repeated=target?view.m_terrainEdit.makeSemanticVertexPaint(8,8,paint,ETerrainTileLayer::Land,1,paintCatalog,secondSeed,&paintReport)
                :view.m_terrainEdit.makeSemanticTilePaint(paint,ETerrainTileLayer::Land,1,paintCatalog,secondSeed,&paintReport);
            else repeated=target?view.m_terrainEdit.makeVirtualMaterialPaint(8,8,paint,ETerrainTileLayer::Land,1,paintCatalog,secondSeed,&paintReport)
                :view.m_terrainEdit.makeVirtualTileMaterialPaint(paint,ETerrainTileLayer::Land,1,paintCatalog,secondSeed,&paintReport);
            view.randomizeTerrainPaintCommand(&repeated,catalog);
            const bool repeatedApplied=view.applyTerrainTilePreview(repeated,true);
            appendTerrainTileCommand(&view.m_tileStrokeCommand,repeated);view.endTileBrushGroup();
            check(repeatedApplied && view.terrainRandomPaintSeed()==stableSeed && snapshot(view.m_terrainEdit)==once,
                QString("stable uniform repeat bytes mode%1 target%2").arg(mode).arg(target));
            history.clear();view.m_terrainEdit=baseline;
            check(once!=before,QString("variant/rotation visible for mode%1 target%2").arg(mode).arg(target));
        }
        view.m_pLand->setCanonicalTerrainModel(nullptr);view.m_pLand->unloadMpr();
    }

    if(fixture.value("real").toBool()) {
        CMprDocument real;STerrainAtlasProfile realProfile;
        const QString root=fixture.value("realRoot").toString();
        const QByteArray sourceBytes=read(root+"/source.mpr");
        const auto atlas=QJsonDocument::fromJson(read(root+"/atlas.json")).object();
        if(!check(real.loadBytes(sourceBytes,root+"/source.mpr",&report)
           && STerrainAtlasProfile::fromJson(atlas.value("catalog").toObject(),&realProfile,&report),"private real map and profile load")) return 1;
        const auto realCatalog=realProfile.usableAnalysis(&report).landCatalog;
        QVector<QImage> images;for(int i=0;i<realProfile.atlasSlotCount;++i)images.append(QImage(root+QString("/tiles/%1.png").arg(i)));
        CTerrainEditModel realModel;realModel.load(real,&report);const auto realBefore=realModel;
        QSet<int> realExcluded;for(const auto& a:real.mapInfo().animatedTiles)for(int frame=0;frame<a.frameCount;++frame)realExcluded.insert(int(a.tileIndex)+frame);
        for(const auto& def:realProfile.slotDefinitions)if(!def.mixedEdgeRecipes.isEmpty())realExcluded.insert(def.slot);
        const QRect realAll(0,0,realModel.tileCountX(),realModel.tileCountY());
        const QRect patch=QRect(88,136,16,16).intersected(realAll);
        check(preview(realModel,images,patch).save(output+"/real-before.png"),"real before PNG");
        auto realCommand=realModel.makeRandomTilePaint(realCatalog,136,realAll,realExcluded);
        realCommand.changesRandomSeed=true;
        realCommand.beforeRandomSeed=realModel.randomPaintSeed();
        realCommand.beforeRandomSeedSet=realModel.hasRandomPaintSeed();
        realCommand.afterRandomSeed=136;
        check(!realCommand.isEmpty() && realModel.apply(realCommand,true,&report),"real entire-map shuffle");
        check(preview(realModel,images,patch).save(output+"/real-after.png"),"real after PNG");
        check(preview(realBefore,images,patch).copy(0,0,512,512).scaled(1024,1024,Qt::IgnoreAspectRatio,Qt::FastTransformation)
            .save(output+"/real-top-left-before-2x.png"),"review top-left 512x512 before enlarged PNG");
        check(preview(realModel,images,patch).copy(0,0,512,512).scaled(1024,1024,Qt::IgnoreAspectRatio,Qt::FastTransformation)
            .save(output+"/real-top-left-after-2x.png"),"review top-left 512x512 after enlarged PNG");
        int transitions=0,changedTransitions=0,previousChangedTransitions=0;
        CMprDocument previous;CTerrainEditModel previousModel;
        const bool previousLoaded=previous.loadBytes(read(output+"/review-before-fix/shuffled.mpr"),"review-previous.mpr",&report)
            && previousModel.load(previous,&report);
        bool realClasses=true,realCorners=true,realOverlay=true,realGeometry=true,realProtected=true;
        for(int y=0;y<realModel.tileCountY();++y)for(int x=0;x<realModel.tileCountX();++x) {
            const auto a=realBefore.packedTile(x,y,ETerrainTileLayer::Land);const auto b=realModel.packedTile(x,y,ETerrainTileLayer::Land);
            realClasses&=realBefore.tileType(a)==realModel.tileType(b);
            realCorners&=realCatalog.gridSignatureForPacked(a)==realCatalog.gridSignatureForPacked(b);
            bool known=false;const auto s=realCatalog.gridSignatureForPacked(a,&known);
            if(known && (s.northWest!=s.northEast || s.northWest!=s.southEast || s.northWest!=s.southWest)) {
                ++transitions;changedTransitions+=a!=b;
                if(previousLoaded)previousChangedTransitions+=a!=previousModel.packedTile(x,y,ETerrainTileLayer::Land);
            }
            realOverlay&=realBefore.packedTile(x,y,ETerrainTileLayer::Overlay)==realModel.packedTile(x,y,ETerrainTileLayer::Overlay);
            if(realExcluded.contains(SMprPackedTile::decode(a).atlasSlot()))realProtected&=a==b;
        }
        for(int y=0;y<realModel.vertexCountY();++y)for(int x=0;x<realModel.vertexCountX();++x)realGeometry&=realModel.height(x,y)==realBefore.height(x,y);
        check(realClasses && realCorners,"real all material corners and runtime classes preserved");
        check(transitions>0 && changedTransitions==0,"real every transition physical slot and rotation unchanged");
        log<<"TRANSITIONS count="<<transitions<<" changed="<<changedTransitions<<" previousChanged="<<previousChangedTransitions<<'\n';
        check(realOverlay && realGeometry && realProtected,"real heights overlays and animation cells preserved");
        bool untouchedBytes=true;
        for(const auto& sector:real.sectors()) {
            auto a=realBefore.sectorPayload(sector.sectorX,sector.sectorY);
            const auto b=realModel.sectorPayload(sector.sectorX,sector.sectorY);
            a.replace(int(sector.landTileOffset),512,b.mid(int(sector.landTileOffset),512));
            untouchedBytes &= a==b;
        }
        check(untouchedBytes,"real every byte outside Ground tile references unchanged");
        CMprEditTransaction transaction;QByteArray candidate;
        check(realModel.appendSectorEdits(&transaction,&report)
            && real.applyTransaction(transaction,&candidate,&report),"ordinary MPR transaction builds");
        CView saveView(nullptr,nullptr);saveView.m_terrainEditAvailable=true;
        SResArchiveNewMember realSeedMember;QByteArray seededCandidate;
        check(saveView.buildTerrainSemanticFieldMember(candidate,QFileInfo(output+"/shuffled.mpr"),
            real.mapInfo().innerNamespace,realProfile.atlasPixelSha256,realProfile.toBytes(),
            &realSeedMember,&report,&realModel,&seededCandidate) && !seededCandidate.isEmpty(),
            "real normal save embeds map seed");
        candidate=seededCandidate;
        check(write(output+"/shuffled.mpr",candidate),"write only private MPR copy");
        CMprDocument reopened;CTerrainEditModel reopenedModel;
        check(reopened.loadBytes(read(output+"/shuffled.mpr"),output+"/shuffled.mpr",&report)
            && reopenedModel.load(reopened,&report) && snapshot(reopenedModel)==snapshot(realModel),"real save reopen exact sector bytes");
        check(saveView.restoreTerrainSemanticFieldFromArchive(reopened,realProfile,&reopenedModel,
            QFileInfo(output+"/shuffled.mpr"),QString(),true)
            && reopenedModel.hasRandomPaintSeed() && reopenedModel.randomPaintSeed()==136
            && reopenedModel.makeRandomTilePaint(realCatalog,136,realAll,realExcluded).isEmpty(),
            "real reopen restores bound seed and identical shuffle");
        check(reopened.mapInfo().tileTypes==real.mapInfo().tileTypes
            && reopened.mapInfo().animatedTiles==real.mapInfo().animatedTiles
            && reopened.mapInfo().materials==real.mapInfo().materials
            && reopened.mapInfo().textureCount==real.mapInfo().textureCount,"real no slot class material or animation metadata growth");
        realModel.apply(realCommand,false,&report);check(snapshot(realModel)==snapshot(realBefore),"real Undo exact bytes");
        realModel.apply(realCommand,true,&report);check(snapshot(realModel)==snapshot(reopenedModel),"real Redo exact bytes");
        // Controlled homogeneous patch, using the real profile and real artwork.
        int uniformSlot=-1;for(const auto& def:realProfile.slotDefinitions) {
            if(def.assigned && !def.ambiguous && def.signature.northWest==def.signature.northEast
               && def.signature.northWest==def.signature.southEast && def.signature.northWest==def.signature.southWest
               && !realExcluded.contains(def.slot) && !images[def.slot].isNull()) {
                bool found=false;const auto v=realCatalog.variantForPacked(packed(def.slot),&found);
                if(found && v.randomRotationAllowed) { uniformSlot=def.slot;break; }
            }
        }
        check(uniformSlot>=0,"real rotatable uniform demonstration source");
        if(uniformSlot>=0) {
            auto demonstration=realBefore;STerrainTileBrush fill;fill.centerX=patch.center().x();fill.centerY=patch.center().y();fill.radius=7.5f;fill.shape=ETerrainTileBrushShape::Square;
            fill.centerX=patch.x()+7.5f;fill.centerY=patch.y()+7.5f;
            demonstration.apply(demonstration.makeTilePaint(fill,ETerrainTileLayer::Land,quint8(uniformSlot/64),quint8(uniformSlot%64),0),true);
            check(preview(demonstration,images,patch).save(output+"/repeat-before.png"),"homogeneous repeat before PNG");
            auto mixed=demonstration.makeRandomTilePaint(realCatalog,136,patch,realExcluded);
            demonstration.apply(mixed,true);check(!mixed.isEmpty() && preview(demonstration,images,patch).save(output+"/repeat-after.png"),"homogeneous repeat after PNG");
        }
        log<<"REAL changed="<<realCommand.deltas.size()<<" cells="<<realAll.width()*realAll.height()<<" slots="<<real.mapInfo().tileTypes.size()<<'\n';
    }
    QJsonObject result;result.insert("passed",passed);result.insert("failed",failed);result.insert("skipped",0);
    QJsonObject distribution;for(auto i=counts.cbegin();i!=counts.cend();++i)distribution.insert(QString::number(i.key()),i.value());result.insert("physicalDistribution",distribution);
    QJsonObject turns;for(auto i=rotations.cbegin();i!=rotations.cend();++i)turns.insert(QString::number(i.key()),i.value());result.insert("rotations",turns);
    write(output+"/result.json",QJsonDocument(result).toJson());
    log<<"SUMMARY "<<passed<<" PASS / "<<failed<<" FAIL / 0 SKIP\n";
    return failed?1:0;
}
