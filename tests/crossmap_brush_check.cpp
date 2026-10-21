#include <QtWidgets>
#include <QtTest>
#include "view.h"
#include "camera.h"
#include "settings.h"
#include "resourcemanager.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "progressview.h"
#include "tree_view.h"
#include "tile_form.h"
#include "tile.h"
#include "endurance_document.h"
#include "neighbor_scene_data.h"
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& text){out<<(ok?"PASS ":"FAIL ")<<text<<"\n";out.flush();if(!ok)throw text;++passes;};
    try {
        const QString fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MIXED_FIXTURE"));
        const QString game=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MIXED_GAME_ROOT"));
        const QString output=QDir::fromNativeSeparators(qEnvironmentVariable("EI_CROSSMAP_OUTPUT"));
        const bool before=qEnvironmentVariableIsSet("EI_CROSSMAP_BEFORE");
        check(!fixture.isEmpty()&&!game.isEmpty()&&!output.isEmpty(),"explicit private fixture and output");
        QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName("Ataman215CrossmapValidation");
        QTemporaryDir scratch;QDir::setCurrent(scratch.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
        CSettings settings;
        dynamic_cast<COptString*>(settings.opt("resourceProfile"))->setValue("deja-vu");
        dynamic_cast<COptString*>(settings.opt("gameRoot"))->setValue(game);
        dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue({fixture+"/res",qEnvironmentVariable("EI_NEIGHBOR_DONOR")+"/MODS/DejaVu/res",qEnvironmentVariable("EI_NEIGHBOR_DONOR")+"/Base/Res"});
        dynamic_cast<COptString*>(settings.opt(eOptSetResource,"terrainAuthoringArchive"))->setValue(qEnvironmentVariable("EI_MIXED_REFERENCE"));
        CTextureList::getInstance()->attachSettings(&settings);
        QUndoStack undo;QTableWidget table;table.setColumnCount(2);CProgressView progress;QLineEdit coords;CTreeView tree;CTileForm tiles;
        CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);view.setMouseTracking(true);
        view.attach(&settings,&table,&undo,&progress,&coords,&tree,&tiles);view.resize(1000,750);view.show();QCoreApplication::processEvents();
        check(view.isValid(),"native hidden production GL");CLandscapeTools tools;view.attachLandscapeTools(&tools);
        QStringList messages;QObject::connect(&view,&CView::updateMsg,[&](const QString& text){messages.append(text);out<<"MESSAGE "<<text<<"\n";out.flush();});
        QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[&](){for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget)){
            out<<"DIALOG "<<box->text()<<"\n";out.flush();
            if(auto* save=box->button(QMessageBox::Save))save->click();else box->reject();
        }});dialogs.start(50);
        const QString activePath=fixture+"/Maps/zone1.mpr",neighborPath=fixture+"/Maps/zone2.mpr";
        const auto active=CWorldTerrainSession::key(activePath),neighbor=CWorldTerrainSession::key(neighborPath);
        view.loadLandscape(QFileInfo(activePath),false);view.makeCurrent();view.paintGL();check(view.ensureTerrainAutoCatalogReady(),"zone1 catalog ready");
        view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborLayoutStorageWritable=true;view.m_neighborLandscapesVisible=true;
        view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();QString error;CValidationReport report;
        check(view.m_worldTerrainSession.load(activePath,&error),"active world loaded");view.syncWorldTerrainActive();check(view.ensureWorldTerrainAtlas(active,&report),"active atlas bound");
        check(view.m_worldTerrainSession.load(neighborPath,&error)&&view.ensureWorldTerrainAtlas(neighbor,&report),"neighbor atlas globalized");
        SNeighborMapPlacement placement;placement.zoneId="gz2g";placement.mprPath=neighborPath;placement.origin={192,288};placement.worldOrigin=placement.origin;
        const auto doc=view.m_worldTerrainSession.documents[neighbor];placement.size={doc->terrain.tileCountX()*2.0,doc->terrain.tileCountY()*2.0};
        view.m_neighborPlacements={placement};view.m_worldTerrainEnabledPaths.insert(neighbor);
        CView::SLoadedNeighborLandscape loaded;loaded.placement=placement;view.makeCurrent();loaded.landscape=CLandscape::createReadOnlyBackground();
        check(loaded.landscape->readBackgroundMap(QFileInfo(neighborPath)),"real zone2 background loaded");loaded.landscape->setWorldOffset({192,288,0});
        loaded.terrainFingerprint=view.neighborTerrainFingerprint(neighborPath,view.neighborResourceFingerprint());
        loaded.objectFingerprint=neighborContentFingerprint(placement.mobPath)+view.neighborObjectResourceFingerprint();loaded.objectsPrepared=true;
        view.m_neighborLandscapes.append(loaded);
        check(view.previewWorldPaintState(neighbor),"neighbor renderer bound to session");
        tools.activateMode(ELandscapeEditMode::TilePaint,true);tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
        check(!view.m_terrainGlobalFamilies.isEmpty(),"shared material palette available");
        out<<"PALETTE families="<<view.m_terrainGlobalFamilies.size()<<"\n";
        if(auto* radius=tools.findChild<QSlider*>("terrainPaintRadius"))radius->setValue(3);
        if(auto* b=tools.findChild<QToolButton*>("vertexPaintTarget"))b->click();
        // Hidden-window front buffers are driver-dependent. Use an owned FBO
        // with the production terrain geometry, atlas and brush draw routines.
        const auto frame=[&](const QVector3D& center,bool physical=false) {
            view.makeCurrent();QOpenGLFramebufferObject fbo(1000,750,QOpenGLFramebufferObject::CombinedDepthStencil);
            QOpenGLShaderProgram shader;
            check(shader.addShaderFromSourceCode(QOpenGLShader::Vertex,
                "#version 120\nattribute vec3 a_position;attribute vec3 a_normal;attribute vec2 a_texture;uniform mat4 u_modelMmatrix;uniform mat4 projection;varying vec2 uv;varying float shade;void main(){uv=a_texture;shade=0.8+0.2*abs(a_normal.z);gl_Position=projection*u_modelMmatrix*vec4(a_position,1.0);}")
                &&shader.addShaderFromSourceCode(QOpenGLShader::Fragment,
                "#version 120\nuniform sampler2D qt_Texture0;uniform bool outline;uniform vec4 customColor;varying vec2 uv;varying float shade;void main(){gl_FragColor=outline?customColor:vec4(texture2D(qt_Texture0,uv).rgb*shade,1.0);}")
                &&shader.link()&&fbo.bind()&&shader.bind(),"diagnostic framebuffer shaders");
            auto* gl=QOpenGLContext::currentContext()->functions();gl->glViewport(0,0,1000,750);gl->glDisable(GL_SCISSOR_TEST);gl->glDisable(GL_BLEND);gl->glDisable(GL_CULL_FACE);gl->glEnable(GL_DEPTH_TEST);gl->glDepthMask(GL_TRUE);gl->glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            gl->glClearColor(0,0,0,1);gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            QMatrix4x4 projection;projection.ortho(center.x()-40,center.x()+40,center.y()-30,center.y()+30,-10000,10000);
            shader.setUniformValue("projection",projection);shader.setUniformValue("outline",false);
            view.m_pLand->draw(&shader);for(const auto& n:view.m_neighborLandscapes)n.landscape->draw(&shader);
            if(physical)view.drawTilePreview(&shader);
            QMatrix4x4 identity;shader.setUniformValue("u_modelMmatrix",identity);shader.setUniformValue("outline",true);
            gl->glBindBuffer(GL_ARRAY_BUFFER,0);gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
            for(int i=0;i<16;++i)gl->glDisableVertexAttribArray(i);
            shader.enableAttributeArray("a_position");view.drawTerrainPaintOutline(&shader);
            const auto image=fbo.toImage();shader.release();fbo.release();return image;
        };
        QVector3D hover(240,302,0),local=hover-QVector3D(192,288,0);check(loaded.landscape->projectPt(local,true),"neighbor hover terrain");hover.setZ(local.z());
        view.m_cam->setLocation(hover+QVector3D(0,0,80),hover,{0,0,0});view.makeCurrent();view.paintGL();
        const auto screen=hover.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));
        const QPoint cursor(qRound(screen.x()),view.height()-qRound(screen.y()));check(view.rect().contains(cursor),"neighbor in viewport");
        bool hit=false;const auto picked=view.getTerrainBrushPos(cursor.x(),cursor.y(),&hit,true);
        out<<"POINTER cursor="<<cursor.x()<<","<<cursor.y()<<" expected="<<hover.x()<<","<<hover.y()<<","<<hover.z()<<" picked="<<picked.x()<<","<<picked.y()<<","<<picked.z()<<" hit="<<hit<<"\n";out.flush();
        check(hit&&qAbs(picked.x()-hover.x())<2&&qAbs(picked.y()-hover.y())<2,"pointer hits neighbor in active coordinates");
        view.updatePreviewTile(picked,true);view.makeCurrent();view.paintGL();
        check(frame(hover).save(output+"/neighbor-hover.png"),"save production geometry and outline framebuffer");
        out<<"HOVER footprint="<<view.m_terrainPaintFootprintValid<<" outline="<<view.m_terrainPaintOutlineValid<<"\n";
        const bool sampled=view.pickTile(hover,true);out<<"SAMPLE result="<<sampled<<"\n";
        if(before){check(!view.m_terrainPaintOutlineValid&&!sampled,"baseline reproduces missing neighbor outline and eyedropper");out<<"PASS baseline "<<passes<<" checks\n";return 0;}
        check(view.m_terrainPaintOutlineValid&&sampled,"neighbor hover and eyedropper work");
        const auto hoverFrame=frame(hover);int outlinePixels=0;
        hoverFrame.save(output+"/neighbor-after-sample.png");
        for(int y=0;y<hoverFrame.height();++y)for(int x=0;x<hoverFrame.width();++x){const QRgb c=hoverFrame.pixel(x,y);if(qRed(c)>240&&qGreen(c)>155&&qGreen(c)<205&&qBlue(c)<50)++outlinePixels;}
        out<<"OUTLINE pixels="<<outlinePixels<<" valid="<<view.m_terrainPaintOutlineValid<<" suspended="<<view.m_landscapeInteractionSuspended<<" cell="<<view.m_terrainPaintFootprint.cellCentered<<" anchor="<<view.m_terrainPaintFootprint.anchor.x()<<","<<view.m_terrainPaintFootprint.anchor.y()<<"\n";
        check(outlinePixels>100,"neighbor outline actually rasterized");
        const qint32 sampledMaterial=tools.paintTerrainType();
        const auto materialAt=[&](const SWorldTerrainDocument& d,int x,int y){
            bool found=false;const auto m=d.terrain.virtualMaterialAt(ETerrainTileLayer::Land,x,y,&found);if(found)return m;
            const auto signature=d.atlas->catalog().gridSignatureForPacked(d.terrain.packedTile(qMin(x,d.terrain.tileCountX()-1),qMin(y,d.terrain.tileCountY()-1),ETerrainTileLayer::Land),&found);
            return found?signature.northWest:qint32(-1);
        };
        check(sampledMaterial==materialAt(*doc,24,7),"eyedropper ID is from zone2 global catalog");
        view.beginTerrainPaintSampling();check(view.pickTile(hover,true),"continuous sample on neighbor");
        check(view.pickTile({48,14,0},true),"continuous sample crosses maps with identical local vertex coordinates");view.endTerrainPaintSampling();
        check(view.pickTile(hover,true)&&tools.paintTerrainType()==sampledMaterial,"restore sampled neighbor brush");
        const auto payloads=[](const SWorldTerrainDocument& d){QByteArray bytes;for(const auto& s:d.baseline.sectors())bytes+=d.terrain.sectorPayload(s.sectorX,s.sectorY);return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);};
        const auto first=view.m_worldTerrainSession.documents[active];
        view.beginTileBrushGroup();view.setTile({220,270,0},true);view.endTileBrushGroup();
        QElapsedTimer finish;finish.start();while(view.m_tileStrokeFinishing&&finish.elapsed()<20000)QTest::qWait(10);
        check(materialAt(*first,110,135)==sampledMaterial,"sampled neighbor material paints active map");
        check(view.pickTile({220,270,0},true)&&tools.paintTerrainType()==sampledMaterial,"active eyedropper regression");
        // A sample with an unknown contextual ID must report it and keep the brush.
        QVector<qint32> field,baseline;
        check(doc->terrain.initializeVirtualMaterialField(ETerrainTileLayer::Land,doc->atlas->catalog(),&report,ETerrainMaterialFieldRecovery::RecoverDeterministically),"neighbor semantic field initialized");
        check(doc->terrain.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&field,&baseline),"copy neighbor field");
        auto missing=field;missing[7*(doc->terrain.tileCountX()+1)+24]=2147483000;
        check(doc->terrain.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,missing,baseline,&report),"install contextual sample fixture");
        messages.clear();check(!view.pickTile(hover,true)&&tools.paintTerrainType()==sampledMaterial&&!messages.isEmpty(),"missing material reports failure without changing brush");
        check(doc->terrain.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,field,baseline,&report),"restore neighbor field");

        tools.activateMode(ELandscapeEditMode::OverlayPaint,true);view.m_terrainPaintProjectionLand=false;
        QVector3D overlayPoint;int overlayRecord=-1;qint32 overlayType=-1;
        for(int y=0;y<doc->terrain.tileCountY()&&overlayRecord<0;++y)
            for(int x=0;x<doc->terrain.tileCountX();++x) {
                const int record=doc->terrain.overlayMaterialIndex(x,y);
                if(record<0||record>=doc->materials.size()||view.m_mprMaterials.indexOf(doc->materials[record])<0)continue;
                QVector3D sourcePoint(x*2+0.25f,y*2+0.25f,0);CTile* sourceTile=nullptr;STileLocation sourceLocation;
                if(!loaded.landscape->pickTile(sourcePoint,sourceTile,sourceLocation,false)
                   ||sourceLocation.xSec*16+sourceLocation.col!=x||sourceLocation.ySec*16+sourceLocation.row!=y)continue;
                bool known=false;const auto sig=doc->atlas->catalog().gridSignatureForPacked(doc->terrain.packedTile(x,y,ETerrainTileLayer::Overlay),&known);
                if(!known||!tools.selectPaintTerrainType(sig.northWest))continue;
                overlayPoint={float(192+x*2)+0.25f,float(288+y*2)+0.25f,0};overlayRecord=record;overlayType=sig.northWest;break;
            }
        check(overlayRecord>=0,"real neighbor has sampleable overlay");
        view.m_terrainPaintProjectionLand=false;
        out<<"OVERLAY point="<<overlayPoint.x()<<","<<overlayPoint.y()<<" record="<<overlayRecord<<" type="<<overlayType<<"\n";out.flush();
        check(view.pickTile(overlayPoint,false)&&tools.paintTerrainType()==overlayType,"neighbor overlay texture sampled into global palette");
        check(tiles.activeMaterialindex()==view.m_mprMaterials.indexOf(doc->materials[overlayRecord]),"overlay record mapped by full value to active table");
        const auto originalRecord=doc->materials[overlayRecord];doc->materials[overlayRecord].opacity+=0.001f;
        messages.clear();check(!view.pickTile(overlayPoint,false)&&!messages.isEmpty()&&tools.paintTerrainType()==overlayType,"missing overlay record is explicit and preserves brush");
        doc->materials[overlayRecord]=originalRecord;
        tools.activateMode(ELandscapeEditMode::TilePaint,true);view.m_terrainPaintProjectionLand=true;

        // Hold one native pointer stroke across the real zone1/zone2 join.
        check(tools.selectPaintTerrainType(1520124769),"select contrasting Stone / Gray");
        QVector3D seam(240,287,0);view.m_pLand->projectPt(seam,true);view.m_cam->setLocation(seam+QVector3D(0,0,80),seam,{0,0,0});view.makeCurrent();view.paintGL();
        const auto p=seam.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));
        const QPoint press(qRound(p.x()),view.height()-qRound(p.y()));
        const auto leftBefore=payloads(*first),rightBefore=payloads(*doc);
        view.updatePreviewTile(seam,true);view.makeCurrent();view.paintGL();const auto seamBeforeFrame=frame(seam);check(seamBeforeFrame.save(output+"/seam-before-stroke.png"),"save seam before stroke");
        QTest::mousePress(&view,Qt::LeftButton,Qt::NoModifier,press);
        check(view.m_tilePaintStrokeActive,"native press owns live stroke");
        const auto leftAfter=payloads(*first),rightAfter=payloads(*doc);
        check(leftAfter!=leftBefore&&rightAfter!=rightBefore,"held stroke changes both maps before release");
        view.makeCurrent();view.paintGL();const auto heldFrame=frame(seam);check(heldFrame.save(output+"/seam-held-stroke.png"),"save live crossmap stroke framebuffer");
        int activePixels=0,neighborPixels=0;for(int y=0;y<heldFrame.height();++y)for(int x=0;x<heldFrame.width();++x)if(heldFrame.pixel(x,y)!=seamBeforeFrame.pixel(x,y)){if(y<375)++neighborPixels;else ++activePixels;}
        check(activePixels>100&&neighborPixels>100,"live framebuffer changes on both sides before release");
        // Independent full publication must render exactly the incremental live result.
        check(view.previewWorldPaintState(active)&&view.previewWorldPaintState(neighbor),"full publication oracle");
        check(frame(seam)==heldFrame,"incremental live pixels equal full renderer publication");
        view.makeCurrent();view.paintGL();
        // Shadow workers may finish between frames; compare document/sector state separately.
        check(payloads(*first)==leftAfter&&payloads(*doc)==rightAfter,"live publication agrees with complete document state");
        QTest::mouseRelease(&view,Qt::LeftButton,Qt::NoModifier,press);
        finish.restart();while(view.m_tileStrokeFinishing&&finish.elapsed()<20000)QTest::qWait(10);
        const auto leftCompleted=payloads(*first),rightCompleted=payloads(*doc);
        check(view.worldTerrainHistory(false,true),"one Undo for crossmap stroke");
        check(payloads(*first)==leftBefore&&payloads(*doc)==rightBefore,"one Undo restores both maps exactly");
        check(view.worldTerrainHistory(true,true),"Redo crossmap stroke");
        check(payloads(*first)==leftCompleted&&payloads(*doc)==rightCompleted,"Redo restores both maps exactly");
        view.updatePreviewTile(seam,true);const auto beforeCacheFrame=frame(seam);
        const auto retained=view.m_neighborLandscapes[0].landscape;
        view.clearNeighborLandscapes();view.loadNeighborLandscapes();view.refreshWorldTerrainNeighbors();
        check(view.m_neighborLandscapes.size()==1&&view.m_neighborLandscapes[0].landscape==retained,"214 reuses painted neighbor renderer from cache");
        check(frame(seam)==beforeCacheFrame,"cached neighbor pixels are current after paint and Redo");

        // C / physical mode still previews and paints the active atlas on neighbor geometry.
        view.selectTerrainPaintPaletteMode(ETerrainPaintPaletteMode::Manual);
        check(tools.manualTileSelectionEnabled(),"C mode selection");
        check(tiles.selectTile(0),"manual authored tile selected");tiles.setTileRotation(2);
        view.updatePreviewTile(hover,true);check(view.m_terrainPaintFootprintValid&&view.isPreviewTile(),"manual physical preview over neighbor");
        view.makeCurrent();view.paintGL();check(frame(hover,true).save(output+"/neighbor-manual-preview.png"),"save manual neighbor preview");
        view.beginTileBrushGroup();view.setTile(hover,true);view.endTileBrushGroup();
        finish.restart();while(view.m_tileStrokeFinishing&&finish.elapsed()<20000)QTest::qWait(10);
        const auto manualPacked=SMprPackedTile::decode(doc->terrain.packedTile(24,7,ETerrainTileLayer::Land));
        check(manualPacked.rotation==2,"manual stroke retains rotation on neighbor");
        check(doc->atlas->activeTileImages()[manualPacked.atlasSlot()]==view.m_workingTerrainAtlas->activeTileImages()[0],"manual stroke imports actual pixels into neighbor atlas");
        check(view.worldTerrainHistory(false,true),"manual crossmap Undo");
        tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);

        QObject::connect(&view,&CView::terrainPublicationPreflightRequested,[&](const QString& path,bool* accepted){*accepted=QFileInfo(path).absoluteFilePath()==QFileInfo(fixture+"/res/HDPack_Terrain.res").absoluteFilePath();});
        check(view.saveWorldTerrainMaps(),"save active and neighbor copies");
        for(const auto& old:{first,doc}) {
            const auto saved=view.m_worldTerrainSession.documents[CWorldTerrainSession::key(old->path)];
            CMprDocument reopened;CTerrainEditModel terrain;CValidationReport validation;
            check(reopened.load(QFileInfo(saved->path),&validation)&&terrain.load(reopened,&validation),"reopen saved MPR: "+QFileInfo(saved->path).fileName());
            bool equal=true;for(const auto& s:reopened.sectors())equal&=terrain.sectorPayload(s.sectorX,s.sectorY)==saved->terrain.sectorPayload(s.sectorX,s.sectorY);
            check(equal,"reopened sectors equal in-memory paint");
        }
        // A newly constructed renderer reads saved pixels/slots, independently of session/cache.
        view.makeCurrent();auto reopened=CLandscape::createReadOnlyBackground();
        check(reopened->readBackgroundMap(QFileInfo(neighborPath)),"fresh renderer reopens saved neighbor");
        QVector3D probe(48,0.5,0);CTile* tile=nullptr;STileLocation location;
        check(reopened->pickTile(probe,tile,location,true),"fresh neighbor renderer tile pick");
        check(tile->tileIndex()==SMprPackedTile::decode(view.m_worldTerrainSession.documents[neighbor]->terrain.packedTile(location.xSec*16+location.col,location.ySec*16+location.row,ETerrainTileLayer::Land)).atlasSlot(),"fresh renderer sees saved neighbor slot");
        view.updatePreviewTile(hover,true);const auto savedFrame=frame(hover);
        const auto displayed=view.m_neighborLandscapes[0].landscape;reopened->setWorldOffset({192,288,0});
        view.m_neighborLandscapes[0].landscape=reopened;
        check(frame(hover)==savedFrame,"saved neighbor pixels equal a fresh renderer from disk");
        view.m_neighborLandscapes[0].landscape=displayed;
        check(view.pickTile(hover,true),"eyedropper remains available after own Save");
        view.m_worldTerrainEnabledPaths.remove(neighbor);view.updatePreviewTile(hover,true);
        check(!view.m_terrainPaintFootprintValid&&!view.pickTile(hover,true),"excluded neighbor is not silently treated as active terrain");
        out<<"PASS crossmap "<<passes<<" checks\n";return 0;
    }catch(const QString& error){out<<"FAIL crossmap "<<error<<"\n";return 1;}
}
