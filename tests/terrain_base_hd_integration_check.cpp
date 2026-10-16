// Native integration against production objects; all writes target an owned map copy.
#include <QtWidgets>
#include <QtOpenGL>
#define private public
#include "landscape.h"
#undef private
#include "view.h"
#include "settings.h"
#include "resourcemanager.h"
#include "progressview.h"
#include "tree_view.h"
#include "tile_form.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "terrain_base_hd.h"
#include "terrain_image_sizes.h"
#include "res_archive_index.h"
#include "mmp_codec.h"
#include "endurance_document.h"

int runScenarioLayerCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw name;++passes;};
    try {
        const QString fixture=qEnvironmentVariable("EI_HD_FIXTURE");
        const QString results=qEnvironmentVariable("EI_HD_OUTPUT");QDir().mkpath(results);
        const QString library=qEnvironmentVariable("EI_HD_LIBRARY");
        QTemporaryDir scratch;check(scratch.isValid(),"isolated settings");
        struct Restore {QString path=QDir::currentPath();~Restore(){QDir::setCurrent(path);}} restore;
        QDir::setCurrent(scratch.path());QApplication::setApplicationName("Ataman190BaseHdCheck");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
        auto* settings=new CSettings;
        dynamic_cast<COptString*>(settings->opt("resourceProfile"))->setValue("deja-vu");
        dynamic_cast<COptString*>(settings->opt("gameRoot"))->setValue("C:/GOG Games/PZ");
        const QStringList roots={fixture+"/res","C:/GOG Games/PZ/MODS/DejaVu/res","C:/GOG Games/PZ/Base/Res"};
        dynamic_cast<COptStringList*>(settings->opt(eOptSetResource,"resourceRoots"))->setValue(roots.toVector());
        CTextureList::getInstance()->attachSettings(settings);
        auto* history=new QUndoStack;auto* table=new QTableWidget;table->setColumnCount(2);
        auto* view=new CView(nullptr,nullptr);auto* picker=new CTileForm;
        view->setAttribute(Qt::WA_DontShowOnScreen);
        view->attach(settings,table,history,new CProgressView,new QLineEdit,new CTreeView,picker);
        view->resize(640,480);view->show();QCoreApplication::processEvents();check(view->isValid(),"hidden desktop OpenGL");
        QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,view,[&](){
            for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){
                out<<"DIALOG "<<box->windowTitle()<<": "<<box->text()<<"\n";out.flush();
                if(box->windowTitle()=="Replace shared terrain atlas pages?"
                   && QFileInfo(view->m_activeTerrainArchivePath).absoluteFilePath()==QFileInfo(fixture+"/res/HDPack_Terrain.res").absoluteFilePath())
                    box->button(QMessageBox::Yes)->click();
                else box->reject();}
        });dismiss.start(100);
        view->makeCurrent();CTextureList::getInstance()->initResource();
        auto* panel=new CLandscapeTools;view->attachLandscapeTools(panel);
        check(panel->findChild<QPushButton*>("convertBaseTerrainToHd")!=nullptr,"dedicated visible button exists");
        const QFileInfo mapPath(fixture+"/Maps/zone19.mpr");
        view->loadLandscape(mapPath,false);view->makeCurrent();
        check(view->ensureTerrainAutoCatalogReady(),"real map material catalog");
        check(view->m_workingTerrainAtlas&&view->m_workingTerrainAtlas->isValid(),"real working atlas");
        const auto originalDocument=view->m_mprDocument;
        const auto originalImages=view->m_workingTerrainAtlas->activeTileImages();
        const auto originalModel=view->m_terrainEdit;
        const auto originalCatalog=view->m_workingTerrainAtlas->catalog();
        const auto originalTypes=view->m_workingTerrainAtlas->activeTileTypes();
        const auto originalAnimations=view->m_mprAnimatedTiles;
        if(qEnvironmentVariableIsSet("EI_HD_GUARDS_ONLY")) {
            const auto fingerprint=view->m_workingTerrainAtlas->stateFingerprint();
            const QDir actual(library);
            const auto currentBytes=[&](){QFile f(actual.filePath("variants/current.json"));f.open(QIODevice::ReadOnly);return f.readAll();}();
            const QString id=QJsonDocument::fromJson(currentBytes).object().value("id").toString();
            const QString shadow=results+"/corrupt-library";QDir().mkpath(shadow+"/variants/"+id);QDir().mkpath(shadow+"/sources");
            auto write=[&](const QString& path,const QByteArray& bytes){QSaveFile f(path);check(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit(),"own guard fixture");};
            auto read=[](const QString& path){QFile f(path);f.open(QIODevice::ReadOnly);return f.readAll();};
            write(shadow+"/variants/current.json",currentBytes);
            write(shadow+"/sources/manifest.json","{\"archives\":[]}");
            write(shadow+"/variants/"+id+"/manifest.json",read(actual.filePath("variants/"+id+"/manifest.json")));
            auto page=read(actual.filePath("variants/"+id+"/variants000.mmp"));check(page.size()>100,"bank page available");page[100]=char(page[100]^1);
            write(shadow+"/variants/"+id+"/variants000.mmp",page);
            std::atomic_bool cancelled(true);
            auto stopped=prepareBaseTerrainHd(shadow,*view->m_workingTerrainAtlas,originalModel,view->m_terrainAtlasProfile,originalAnimations,190,&cancelled);
            check(stopped.error=="Cancelled"&&!stopped.atlas,"cancel does not produce a transaction");
            cancelled=false;
            auto damaged=prepareBaseTerrainHd(shadow,*view->m_workingTerrainAtlas,originalModel,view->m_terrainAtlasProfile,originalAnimations,190,&cancelled);
            check(damaged.error.contains("hash mismatch")&&!damaged.atlas,"same-size damaged bank refused");
            check(fingerprint==view->m_workingTerrainAtlas->stateFingerprint(),"guards leave live atlas untouched");
            CMprDocument initial;CValidationReport report;
            check(initial.load(QFileInfo(qEnvironmentVariable("EI_HD_ORIGINAL_MPR")),&report),"original map for coverage audit");
            check(initial.mapInfo().materials==view->m_mprMaterials&&initial.mapInfo().animatedTiles==view->m_mprAnimatedTiles,"saved materials and animations exact");
            CTerrainEditModel originalTerrain;check(originalTerrain.load(initial,&report),"original terrain for coverage audit");
            originalTerrain.enableWorkingAtlasSlots(view->m_workingTerrainAtlas->capacityLimit());
            const auto audit=prepareBaseTerrainHd(library,*view->m_workingTerrainAtlas,originalTerrain,view->m_terrainAtlasProfile,originalAnimations,190);
            check(audit.error.isEmpty(),"read-only coverage audit");
            QFile coverage(results+"/coverage.txt");check(coverage.open(QIODevice::WriteOnly),"coverage output");
            coverage.write(audit.unmatchedSources.join('\n').toUtf8());
            out<<"PASS guards "<<passes<<" checks\n";return 0;
        }
        const QString resourcePath=fixture+"/res/HDPack_Terrain.res";
        CResArchiveIndex originalArchive;QString error;check(originalArchive.open(resourcePath,&error),"private atlas copy");
        QVector<QByteArray> oldPages;
        for(int page=0;page<int(originalDocument.mapInfo().textureCount);++page)
            oldPages.append(originalArchive.readMember(QStringLiteral("%1%2.mmp").arg(originalDocument.mapInfo().innerNamespace).arg(page,3,10,QLatin1Char('0')),&error));
        for(const auto& page:oldPages)check(!page.isEmpty(),"original page captured");
        auto modelBytes=[&](){QByteArray bytes;for(const auto& sector:originalDocument.sectors())bytes+=view->m_terrainEdit.sectorPayload(sector.sectorX,sector.sectorY);return bytes;};
        const auto before=modelBytes();
        view->grabFrameBuffer().save(results+"/before.png");
        QString summary;
        const bool converted=view->convertBaseTerrainToHd(library,190,&summary);
        out<<summary<<"\n";out.flush();check(converted,"production HD conversion");
        const auto after=modelBytes();check(after!=before,"actual map references changed");
        check(view->m_workingTerrainAtlas->authoredTileImages()==originalImages,"native authored pixels and sizes unchanged");
        const auto active=view->m_workingTerrainAtlas->activeTileImages();
        const auto types=view->m_workingTerrainAtlas->activeTileTypes();
        int changed=0;QSet<int> outputs;QSet<int> protectedSlots;QMap<int,QSet<int>> distribution;
        QPoint example(-1,-1);
        for(const auto& a:originalAnimations)for(int i=0;i<qMax(1,int(a.frameCount));++i)protectedSlots.insert(a.tileIndex+i);
        for(int y=0;y<originalModel.tileCountY();++y)for(int x=0;x<originalModel.tileCountX();++x){
            bool a=false,b=false;const auto old=originalModel.packedTile(x,y,ETerrainTileLayer::Land,&a);
            const auto now=view->m_terrainEdit.packedTile(x,y,ETerrainTileLayer::Land,&b);
            if(a!=b)throw QString("land availability changed");
            if(old!=now){
                const int oldSlot=SMprPackedTile::decode(old).atlasSlot(),newSlot=SMprPackedTile::decode(now).atlasSlot();
                bool known=false;const auto s=originalCatalog.gridSignatureForPacked(old,&known);
                if(!known||s.northWest!=s.northEast||s.northWest!=s.southWest||s.northWest!=s.southEast||protectedSlots.contains(oldSlot))throw QString("transition/animation changed");
                if(types.value(newSlot,-1)!=originalTypes.value(oldSlot,-2)||active.at(newSlot).size()!=QSize(256,256))throw QString("HD class or size mismatch");
                ++changed;outputs.insert(newSlot);distribution[oldSlot].insert(newSlot);
                if(example.x()<0)example=QPoint(x,y);
            }
            const auto oldOverlay=originalModel.packedTile(x,y,ETerrainTileLayer::Overlay,&a);
            const auto newOverlay=view->m_terrainEdit.packedTile(x,y,ETerrainTileLayer::Overlay,&b);
            if(a!=b||oldOverlay!=newOverlay)throw QString("overlay changed");
        }
        check(changed>0&&outputs.size()>1,"HD variants distributed; only uniform land changes; classes and overlays exact");
        int diversified=0;for(const auto& pool:distribution)if(pool.size()>1)++diversified;
        check(diversified>0,"multiple variants used for occurrences of the same original tile");
        auto gpuCheck=[&](const QVector<QImage>& native){
            view->makeCurrent();auto* texture=view->m_pLand->m_texture;
            const int pages=native.size()/64;const int columns=view->m_pLand->m_renderAtlasColumns;
            const QImage expected=CTerrainVirtualAtlas::composeAtlasGrid(native,pages,columns);
            check(texture&&texture->width()==expected.width()&&texture->height()==expected.height(),"GPU atlas retains maximum native resolution");
            QImage actual(expected.size(),QImage::Format_RGBA8888);texture->bind();
            glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,actual.bits());
            check(glGetError()==GL_NO_ERROR,"GPU readback");
            check(actual==expected,"GPU pixels match full-resolution HD and nearest native SD");
        };
        gpuCheck(active);
        const auto textureId=view->m_pLand->m_texture->textureId();
        auto preview=active;const int changedSlot=*outputs.constBegin();
        preview[changedSlot].setPixelColor(80,80,QColor(255,0,255));
        check(view->m_pLand->previewTerrainAtlas(preview,preview.size()/64,{changedSlot},active),"mixed atlas changed-slot preview");
        check(view->m_pLand->m_texture->textureId()==textureId,"mixed preview reuses GPU texture");
        gpuCheck(preview);
        check(view->m_pLand->previewTerrainAtlas(active,active.size()/64,{changedSlot},preview),"restore mixed preview");
        gpuCheck(active);
        // CPU texture-field comparison is explicitly not a screenshot/game acceptance.
        QImage comparison(12*224,6*224,QImage::Format_RGB32);comparison.fill(Qt::black);QPainter painter(&comparison);
        for(int side=0;side<2;++side)for(int y=0;y<6;++y)for(int x=0;x<6;++x){
            int cx=qBound(0,example.x()+x-2,originalModel.tileCountX()-1),cy=qBound(0,example.y()+y-2,originalModel.tileCountY()-1);
            const auto packed=SMprPackedTile::decode((side?view->m_terrainEdit:originalModel).packedTile(cx,cy,ETerrainTileLayer::Land));
            const auto& tile=(side?active:originalImages).at(packed.atlasSlot());
            QImage oriented=tile.transformed(QTransform().rotate(90*packed.rotation));
            int inset=oriented.width()/16;oriented=oriented.copy(inset,inset,oriented.width()-2*inset,oriented.height()-2*inset);
            painter.drawImage(QRect((side*6+x)*224,y*224,224,224),oriented);
        }
        painter.end();comparison.save(results+"/cpu-field-before-after.png");
        history->undo();check(modelBytes()==before,"Undo exact original sectors");
        history->redo();check(modelBytes()==after,"Redo exact chosen layout");
        view->makeCurrent();view->grabFrameBuffer().save(results+"/after.png");
        check(view->currentTerrainMprSaveRoute(false)==ETerrainMprSaveRoute::SaveEditableModernDraft,"save uses native-page preservation");
        check(QFileInfo(view->m_activeTerrainArchivePath).absoluteFilePath()==QFileInfo(resourcePath).absoluteFilePath(),"save destination is the private archive");
        check(view->saveChangedLandscapeDocument(mapPath,true),"production Save and baseline adoption");
        CResArchiveIndex saved;check(saved.open(resourcePath,&error),"reopen saved private archive");
        for(int page=0;page<oldPages.size();++page)
            check(saved.readMember(QStringLiteral("%1%2.mmp").arg(originalDocument.mapInfo().innerNamespace).arg(page,3,10,QLatin1Char('0')),&error)==oldPages[page],"original MMP page byte-identical after Save");
        check(modelBytes()==after,"Save preserves chosen cells");
        history->undo();check(modelBytes()==before,"Undo after Save");
        history->redo();check(modelBytes()==after,"Redo after Save");
        check(view->saveChangedLandscapeDocument(mapPath,true),"second ordinary Save");
        view->unloadLand(false);view->makeCurrent();view->loadLandscape(mapPath,false);view->makeCurrent();
        check(view->ensureTerrainAutoCatalogReady(),"fresh map reopen");
        check(modelBytes()==after,"reopened exact cell layout");
        const auto reopened=view->m_workingTerrainAtlas->activeTileImages();
        check(reopened.mid(0,originalImages.size())==originalImages,"reopened native water/transition images and dimensions exact");
        gpuCheck(reopened);
        for(int slot:outputs)check(reopened.at(slot).size()==QSize(256,256),"reopened HD tile remains256");
        CResArchiveIndex twice;check(twice.open(resourcePath,&error),"second saved archive open");
        for(int page=0;page<oldPages.size();++page)
            check(twice.readMember(QStringLiteral("%1%2.mmp").arg(originalDocument.mapInfo().innerNamespace).arg(page,3,10,QLatin1Char('0')),&error)==oldPages[page],"native page remains byte-identical after second Save");
        view->grabFrameBuffer().save(results+"/reopened.png");
        QJsonObject receipt{{"passes",passes},{"changedCells",changed},{"distinctOutputs",outputs.size()},{"summary",summary},{"ownerVisualAcceptance",false}};
        QFile output(results+"/receipt.json");check(output.open(QIODevice::WriteOnly),"write receipt");output.write(QJsonDocument(receipt).toJson());
        out<<"PASS base HD native integration "<<passes<<" checks\n";return 0;
    }catch(const QString& error){out<<"FAIL base HD: "<<error<<"\n";out.flush();return 1;}
}
