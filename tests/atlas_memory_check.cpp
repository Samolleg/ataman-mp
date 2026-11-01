#include <QtWidgets>
#include <QtTest>
#include <QOpenGLFunctions_1_1>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "tile_form.h"
#include "tile.h"
#include "terrain_virtual_atlas.h"
#include "render_memory.h"
#include "resourcemanager.h"
#include "res_file.h"
#include "mmp_codec.h"

namespace {
QTextStream out(stdout);int passes=0;
void check(bool ok,const QString& text){out<<(ok?"PASS ":"FAIL ")<<text<<'\n';out.flush();if(!ok)throw text;++passes;}
QByteArray pixels(QOpenGLTexture* texture) {
    auto* gl=QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_1_1>();
    check(gl&&gl->initializeOpenGLFunctions(),"GPU readback available");
    QCryptographicHash hash(QCryptographicHash::Sha256);texture->bind();
    for(int level=0;level<texture->mipLevels();++level) {
        QByteArray data(qMax(1,texture->width()>>level)*qMax(1,texture->height()>>level)*4,0);
        gl->glGetTexImage(GL_TEXTURE_2D,level,GL_RGBA,GL_UNSIGNED_BYTE,data.data());hash.addData(data);
    }
    return hash.result();
}
class Hidden:public QObject {
    bool eventFilter(QObject* object,QEvent* event)override {
        if(event->type()==QEvent::Polish||event->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(object))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
        if(event->type()==QEvent::Show)if(auto* d=qobject_cast<QMessageBox*>(object))QTimer::singleShot(0,d,&QDialog::reject);
        return false;
    }
};
}
int runAssemblyCatalogCheck() {
    try {
        const auto root=qEnvironmentVariable("EI_AUDIT_FIX_ROOT");check(!root.isEmpty(),"isolated root");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setQuitOnLastWindowClosed(false);
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);
        window.resize(800,600);window.show();qApp->processEvents();auto& view=*window.view();view.makeCurrent();
        check(view.isValid(),"native renderer");auto* land=view.m_pLand;
        land->readMap(QFileInfo(qEnvironmentVariable("EI_AUDIT_FIX_MPR")),false);
        check(land->isMprLoad(),"synthetic terrain loaded");
        {
            CResFile archive;QVector<QImage> decoded;
            for(int i=0;i<2;++i){
                QImage page(i?128:64,i?128:64,QImage::Format_RGBA8888);
                for(int y=0;y<page.height();++y)for(int x=0;x<page.width();++x)page.setPixelColor(x,y,QColor((x*3)%256,(y*7)%256,40+i*100));
                QString error;const auto mmp=ei::encodeMmpDxt1(page,true,&error);check(!mmp.isEmpty(),"synthetic mixed-size MMP encoded");
                archive.addFiledata(QString("au260%1.mmp").arg(i,3,10,QChar('0')),mmp);
                auto prepared=CTextureList::prepareStaticTexture(mmp);check(prepared.valid(),"independent mixed page decoded");
                decoded.append(prepared.image.convertToFormat(QImage::Format_RGBA8888).scaled(128,128,Qt::IgnoreAspectRatio,Qt::FastTransformation));
            }
            QFile file(root+"/mixed.res");const auto bytes=archive.generateResData();check(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size(),"synthetic mixed archive written");file.close();
            int count=0,columns=0;auto* texture=CTextureList::getInstance()->buildLandTex("au260",2,count,columns,file.fileName());
            check(texture&&count==2,"mixed resource loader returns complete atlas");
            QImage expected(columns*128,((count+columns-1)/columns)*128,QImage::Format_RGBA8888);expected.fill(Qt::transparent);
            for(int page=0;page<count;++page)for(int y=0;y<128;++y)
                memcpy(expected.scanLine((page/columns)*128+y)+(page%columns)*128*4,decoded[page].constScanLine(y),128*4);
            QOpenGLTexture oracle(expected,QOpenGLTexture::GenerateMipMaps);
            check(pixels(texture)==pixels(&oracle),"mixed native atlas pixels/mips equal former full-image layout");
            CTextureList::getInstance()->releaseTerrainTexture(texture);
        }
        for(bool mixed:{false,true}) {
            QVector<QImage> tiles;
            for(int i=0;i<320;++i) {
                const int size=mixed&&i<64?16:32;QImage tile(size,size,QImage::Format_RGBA8888);
                for(int y=0;y<size;++y)for(int x=0;x<size;++x)tile.setPixelColor(x,y,QColor((i*5+x)%256,(i*13+y)%256,(x+y*7)%256,255));
                tiles.append(tile);
            }
            const auto image=CTerrainVirtualAtlas::composeAtlasGrid(tiles,5,CTile::preferredAtlasColumns(5));
            QOpenGLTexture oracle(image,QOpenGLTexture::GenerateMipMaps);
            check(oracle.isCreated(),"reference texture created");
            check(land->previewTerrainAtlas(tiles,5),"page upload "+QString::number(mixed));
            check(pixels(&oracle)==pixels(land->glTexture()),"all mip levels equal full-image oracle "+QString::number(mixed));
            auto changed=tiles;changed[75].fill(Qt::magenta);auto old=land->glTexture()->textureId();
            check(land->previewTerrainAtlas(changed,5,{75},tiles),"in-place follow-up publication");
            check(land->glTexture()->textureId()==old,"same-layout GPU allocation retained");
            check(land->previewTerrainAtlas(tiles,5,{75},changed)&&pixels(&oracle)==pixels(land->glTexture()),"rollback pixels and mipmaps exact");
            land->clearPreviewTerrainAtlas();
        }
        const auto map=qEnvironmentVariable("EI_AUDIT_ZONE9_MPR");check(!map.isEmpty(),"explicit private zone9");
        QElapsedTimer load;load.start();window.openMapPair(map,QString(),"zone9");
        check(view.ensureTerrainAutoCatalogReady()&&view.m_workingTerrainAtlas,"zone9 canonical catalog ready");view.makeCurrent();
        const auto loadMs=load.elapsed();check(!land->captureUsesPreviewAtlas(),"unchanged zone9 retains native base without duplicate preview");
        SRenderMemoryEstimate memory;land->accumulateMemory(memory);
        const auto baseTexture=land->captureSourceAtlas();const auto baseline=view.m_mprDocument.originalSha256();
        auto stage=view.m_workingTerrainAtlas->createStagingCopy();auto imported=stage->authoredTileImages().first().copy();imported.fill(QColor(19,41,173));
        CValidationReport report;const int slot=stage->importPhysicalTile(imported,0,nullptr,"synthetic",0,&report,true);
        check(slot>=0,"prepare first new tile in detached working atlas");
        QElapsedTimer publish;publish.start();check(view.syncWorkingTerrainAtlas(&report,stage.data()),"first edited atlas publishes lazily: "+report.toText());
        check(land->captureUsesPreviewAtlas()&&land->captureSourceAtlas()==baseTexture&&view.m_mprDocument.originalSha256()==baseline,"publication retains original resource and MPR baseline");
        const auto publishMs=publish.elapsed();
        view.makeCurrent();
        check(land->previewTerrainAtlasIfChanged(view.m_workingTerrainAtlas->activeTileImages(),view.m_workingTerrainAtlas->activeTextureCount()),"first publication can restore original working atlas");
        land->clearPreviewTerrainAtlas();
        check(view.m_pTileForm->adoptTileImages(view.m_workingTerrainAtlas->activeTileImages(),view.m_workingTerrainAtlas->activeTileTypes()),"original picker restored");
        // Exercise the normal brush and the unified Undo path after lazy load.
        view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborLayoutStorageWritable=true;
        view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();
        auto* panel=view.m_landscapeTools;panel->activateMode(ELandscapeEditMode::TilePaint,true);panel->setPaintPaletteMode(ETerrainPaintPaletteMode::Manual);
        const auto before=view.m_terrainEdit.sectorPayload(0,0);const auto current=SMprPackedTile::decode(view.m_terrainEdit.packedTile(5,5,ETerrainTileLayer::Land)).atlasSlot();
        check(view.m_pTileForm->selectTile(current==0?1:0),"select different authored tile");
        view.beginTileBrushGroup();check(view.paintWorldTerrainAt({10,10,0},true),"normal first brush accepted");view.endTileBrushGroup();
        const auto after=view.m_terrainEdit.sectorPayload(0,0);check(after!=before,"first brush changes canonical terrain");
        window.on_actionUndo_triggered();check(view.m_terrainEdit.sectorPayload(0,0)==before,"first brush Undo exact");
        window.on_actionRedo_triggered();check(view.m_terrainEdit.sectorPayload(0,0)==after,"first brush Redo exact");window.on_actionUndo_triggered();
        out<<"MEASURE loadMs="<<loadMs<<" firstAtlasPublicationMs="<<publishMs<<" baseAtlasGpuBytes="<<memory.gpuTerrainTextureBytes
           <<" cpuTerrainBytes="<<memory.cpuTerrainBytes<<" duplicatePreviewAtOpen=0\n";
        window.m_undoStack->clear();view.m_worldTerrainSession.clear();land->setDirty(false);
        out<<passes<<" PASS / 0 FAIL\n";out.flush();return 0;
    }catch(const QString& error){out<<"FAIL "<<error<<'\n';out.flush();return 1;}
}
