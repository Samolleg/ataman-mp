#include "view.h"
#include "terrain_image_sizes.h"
#include "camera.h"
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QOpenGLFramebufferObject>
#include "settings.h"
#include "landscape_tools.h"
#include "tree_view.h"
#include "progressview.h"
#include "tile_form.h"
#include "landscape.h"
#include "worldobj.h"
#include "resourcemanager.h"
#include "endurance_document.h"
#include "terrain_atlas_archive_binding.h"
#include "mmp_codec.h"
#include <QElapsedTimer>
#include <QThread>
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QUndoStack>
#include <QLineEdit>
#include <QSlider>
#include <QToolButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QAbstractButton>
#include <QTimer>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPainter>
#include <QCryptographicHash>

int runNeighborPerformanceCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");
    const QString output=qEnvironmentVariable("EI_NEW_PAINT_OUTPUT");QDir().mkpath(output);
    QTemporaryDir scratch;if(!scratch.isValid())return 1;
    struct Restore{QString path=QDir::currentPath();~Restore(){QDir::setCurrent(path);}}restore;
    QDir::setCurrent(scratch.path());QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
    CSettings settings;auto* roots=dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"));if(!roots)return 2;
    roots->setValue(qEnvironmentVariable("EI_NEIGHBOR_PERF_RES").split(';',QString::SkipEmptyParts).toVector());
    auto* referenceOption=dynamic_cast<COptString*>(settings.opt(eOptSetResource,"terrainAuthoringArchive"));
    if(!referenceOption)return 40;
    referenceOption->setValue(qEnvironmentVariable("EI_AUTHORING_REFERENCE"));
    CTextureList::getInstance()->attachSettings(&settings);CObjectList::getInstance()->attachSettings(&settings);
    QUndoStack history;CTreeView tree;QTableWidget properties;properties.setColumnCount(2);
    CProgressView progress;QLineEdit coordinates;CTileForm tiles;CView view(nullptr,nullptr);
    view.setAttribute(Qt::WA_DontShowOnScreen);view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
    view.resize(640,480);view.show();QCoreApplication::processEvents();
    if(!view.isValid()){out<<"FAIL hidden desktop GL\n";return 3;}
    QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,&view,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){out<<"DIALOG "<<box->text()<<"\n";out.flush();if(auto* save=box->button(QMessageBox::Save))save->click();else box->reject();}});dismiss.start(50);
    CLandscapeTools tools;view.attachLandscapeTools(&tools);
    const QString path=qEnvironmentVariable("EI_NEIGHBOR_PERF_ROOT")+"/Maps/zone13.mpr";
    view.loadLandscape(QFileInfo(path),false);
    if(!view.ensureTerrainAutoCatalogReady()||!view.m_terrainEditAvailable){out<<"FAIL active map/catalog\n";return 4;}
    const auto id=CWorldTerrainSession::key(path);QString error;CValidationReport validation;
    view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborLayoutStorageWritable=true;
    view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();
    if(!view.m_worldTerrainSession.load(path,&error)){out<<error;return 5;}view.syncWorldTerrainActive();
    if(!view.ensureWorldTerrainAtlas(id,&validation)){out<<validation.toText();return 6;}
    const qint32 stone=1520124769,snow=1960116145;
    for(const auto& f:view.m_terrainGlobalFamilies)if(f.id==stone||f.id==snow)out<<"MATERIAL "<<f.id<<" "<<f.name<<"\n";
    tools.activateMode(ELandscapeEditMode::TilePaint,true);tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
    if(!tools.selectPaintTerrainType(stone)){out<<"FAIL select Stone / Gray\n";return 7;}
    auto* radius=tools.findChild<QSlider*>("terrainPaintRadius");if(!radius)return 8;radius->setValue(1);
    for(auto* b:tools.findChildren<QToolButton*>())if(b->objectName()=="vertexPaintTarget")b->click();
    if(auto* b=tools.findChild<QCheckBox*>("modernAutoResolveBlends"))b->setChecked(true);
    const QString otherPath=qEnvironmentVariable("EI_NEIGHBOR_PERF_ROOT")+"/Maps/bz10k.mpr";
    const auto otherId=CWorldTerrainSession::key(otherPath);
    if(!view.m_worldTerrainSession.load(otherPath,&error)||!view.ensureWorldTerrainAtlas(otherId,&validation))return 80;
    auto first=view.m_worldTerrainSession.documents[id],second=view.m_worldTerrainSession.documents[otherId];
    const int edge=first->terrain.tileCountX(),cy=qMin(first->terrain.tileCountY(),second->terrain.tileCountY())/2;
    for(const auto& d:{first,second}) {
        const auto cat=d->atlas->catalog();STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=snow;
        bool known=false;const auto p=cat.select(pure,0,0,0,&known);if(!known)return 81;
        STerrainTileCommand patch;
        const int x0=d==first?edge-4:0,x1=d==first?edge:4;
        for(int y=cy-6;y<=cy+6;++y)for(int x=x0;x<x1;++x) {
            STerrainTileDelta delta;delta.globalX=x;delta.globalY=y;delta.layer=ETerrainTileLayer::Land;
            delta.before=d->terrain.packedTile(x,y,delta.layer);delta.after=p;patch.deltas.append(delta);
        }
        if(!d->terrain.apply(patch,true,&validation)
           ||!d->terrain.initializeVirtualMaterialField(ETerrainTileLayer::Land,cat,&validation,ETerrainMaterialFieldRecovery::RecoverDeterministically))return 82;
        QVector<qint32> current,baseline;
        if(!d->terrain.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&current,&baseline))return 83;
        const int stride=d->terrain.tileCountX()+1;
        for(int y=cy-6;y<=cy+7;++y)for(int x=x0;x<=x1;++x)current[y*stride+x]=baseline[y*stride+x]=snow;
        if(!d->terrain.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,current,baseline,&validation))return 84;
    }
    view.m_terrainEdit=first->terrain;if(!view.previewWorldPaintState(id))return 85;
    SNeighborMapPlacement placement;placement.zoneId="cross-bz10k";placement.mprPath=otherPath;
    placement.worldOrigin={edge*2.0,0};placement.origin=placement.worldOrigin;
    placement.size={second->terrain.tileCountX()*2.0,second->terrain.tileCountY()*2.0};
    view.m_neighborPlacements={placement};view.m_worldTerrainEnabledPaths.insert(otherId);
    const auto payloads=[](const SWorldTerrainDocument& d){QMap<QPair<int,int>,QByteArray> r;for(const auto& sec:d.baseline.sectors())r[qMakePair(int(sec.sectorX),int(sec.sectorY))]=d.terrain.sectorPayload(sec.sectorX,sec.sectorY);return r;};
    const auto appearance=[](const SWorldTerrainDocument& document) {
        const auto images=terrainDisplayTiles(document.atlas->activeTileImages());
        const auto types=document.atlas->activeTileTypes();
        QMap<quint16,QByteArray> tiles;QCryptographicHash field(QCryptographicHash::Sha256);
        for(auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay})
            for(int y=0;y<document.terrain.tileCountY();++y)for(int x=0;x<document.terrain.tileCountX();++x) {
                bool valid=false;const auto packed=document.terrain.packedTile(x,y,layer,&valid);
                if(!valid || (layer==ETerrainTileLayer::Overlay&&packed==0xffff)){field.addData("absent",6);continue;}
                if(!tiles.contains(packed)) {
                    const auto decoded=SMprPackedTile::decode(packed);const int slot=decoded.atlasSlot();
                    if(slot<0||slot>=images.size())return QByteArray();
                    const auto image=images[slot].transformed(QTransform().rotate(90*decoded.rotation)).convertToFormat(QImage::Format_RGBA8888);
                    QCryptographicHash pixel(QCryptographicHash::Sha256);
                    pixel.addData(QByteArray::number(types.value(slot,-1))+":"+QByteArray::number(image.width())+"x"+QByteArray::number(image.height())+"\n");
                    for(int row=0;row<image.height();++row)pixel.addData(reinterpret_cast<const char*>(image.constScanLine(row)),image.width()*4);
                    tiles.insert(packed,pixel.result());
                }
                field.addData(tiles.value(packed));
            }
        return field.result().toHex();
    };
    const auto firstBefore=payloads(*first),secondBefore=payloads(*second);
    // Load the second renderer as well: document-only neighbors miss the
    // expensive publication path exercised by the real common brush.
    CView::SLoadedNeighborLandscape loaded;loaded.placement=placement;
    view.makeCurrent();loaded.landscape=CLandscape::createReadOnlyBackground();
    if(!loaded.landscape->readBackgroundMap(QFileInfo(otherPath)))return 92;
    loaded.landscape->setWorldOffset(QVector3D(edge*2,0,0));
    view.m_neighborLandscapes.append(loaded);
    if(!view.previewWorldPaintState(otherId))return 93;
    const auto textureHash=[&](CLandscape* land) {
        view.makeCurrent();auto* tex=land->glTexture();
        typedef void (APIENTRY *ReadTexture)(GLenum,GLint,GLenum,GLenum,void*);
        auto read=reinterpret_cast<ReadTexture>(QOpenGLContext::currentContext()->getProcAddress("glGetTexImage"));
        if(!read || !tex)return QByteArray();
        auto* gl=QOpenGLContext::currentContext()->functions();GLint prior=0;
        gl->glGetIntegerv(GL_TEXTURE_BINDING_2D,&prior);gl->glBindTexture(GL_TEXTURE_2D,tex->textureId());
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for(int level=0;level<tex->mipLevels();++level) {
            QByteArray bytes(qMax(1,tex->width()>>level)*qMax(1,tex->height()>>level)*4,0);
            read(GL_TEXTURE_2D,level,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());hash.addData(bytes);
        }
        gl->glBindTexture(GL_TEXTURE_2D,GLuint(prior));return hash.result().toHex();
    };
    const auto firstTextureBefore=textureHash(view.m_pLand),secondTextureBefore=textureHash(loaded.landscape.data());
    const auto shadowPixels=[&](CLandscape* land) {
        view.makeCurrent();
        const QVector3D sun=QVector3D(0.9f,0.2f,-0.4f).normalized();
        QElapsedTimer ready;ready.start();
        while(ready.elapsed()<1800) {
            land->updateTerrainShadowVisibility(sun,true);QThread::msleep(10);
        }
        view.m_landProgram.bind();
        auto* gl=QOpenGLContext::currentContext()->functions();
        GLint active=0;gl->glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        gl->glActiveTexture(GL_TEXTURE6);
        GLint prior=0;gl->glGetIntegerv(GL_TEXTURE_BINDING_2D,&prior);
        gl->glBindTexture(GL_TEXTURE_2D,0);
        land->applyTerrainShadowUniforms(&view.m_landProgram,true);
        gl->glActiveTexture(GL_TEXTURE6);
        GLint texture=0;gl->glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        typedef void (APIENTRY *ReadTexture)(GLenum,GLint,GLenum,GLenum,void*);
        auto read=reinterpret_cast<ReadTexture>(QOpenGLContext::currentContext()->getProcAddress("glGetTexImage"));
        QByteArray result;
        if(texture && read) {
            QByteArray pixels((land->widthInWorldUnits()+1)*(land->heightInWorldUnits()+1)*4,0);
            read(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            result=QCryptographicHash::hash(pixels,QCryptographicHash::Sha256).toHex();
        }
        gl->glBindTexture(GL_TEXTURE_2D,GLuint(prior));gl->glActiveTexture(GLenum(active));
        return result;
    };
    const auto firstShadowBefore=shadowPixels(view.m_pLand),secondShadowBefore=shadowPixels(loaded.landscape.data());
    if(firstShadowBefore.isEmpty() || secondShadowBefore.isEmpty())return 115;

    QElapsedTimer timer;timer.start();view.beginTileBrushGroup();
    view.m_tileStrokeDistributionSeed=123456789u;
    view.m_workingTerrainAtlas->setDistributionSeed(view.m_tileStrokeDistributionSeed);
    QJsonArray stampMs;
    const auto firstShadowGeneration=view.m_pLand->shadowGeneration();
    const auto secondShadowGeneration=loaded.landscape->shadowGeneration();
    QElapsedTimer stampTimer;stampTimer.start();
    for(int dy=-3;dy<=3;++dy) {
        stampTimer.restart();
        if(!view.paintWorldTerrainAt(QVector3D(edge*2,(cy+dy)*2,0),true))return 86;
        stampMs.append(stampTimer.elapsed());
    }
    view.endTileBrushGroup();first=view.m_worldTerrainSession.documents[id];second=view.m_worldTerrainSession.documents[otherId];
    const qint64 brushMs=timer.elapsed();
    const auto shadowInvalidations=view.m_pLand->shadowGeneration()-firstShadowGeneration
        +loaded.landscape->shadowGeneration()-secondShadowGeneration;
    out<<"Texture-only paint shadow invalidations: "<<shadowInvalidations<<"\n";out.flush();
#ifdef EISHADOW_CANDIDATE
    if(shadowInvalidations!=0)return 114;
#endif
    int changed=0;QJsonArray cells;
    QImage canvas(6*128,11*128,QImage::Format_RGBA8888);canvas.fill(Qt::black);QPainter painter(&canvas);
    for(int y=-5;y<=5;++y)for(int x=-3;x<=2;++x) {
        const bool left=x<0;const auto d=left?first:second;const int tx=left?edge+x:x,ty=cy+y;
        const auto packed=SMprPackedTile::decode(d->terrain.packedTile(tx,ty,ETerrainTileLayer::Land));
        const auto raw=d->atlas->tileImages()[packed.atlasSlot()];const int inset=raw.width()/16;
        const auto rendered=raw.copy(inset,inset,raw.width()-2*inset,raw.height()-2*inset).scaled(128,128)
            .transformed(QTransform().rotate(90*packed.rotation),Qt::FastTransformation).mirrored(false,true);
        painter.drawImage((x+3)*128,(y+5)*128,rendered);
        if((left&&x!=-1)||(!left&&x!=0)||y<-4||y>3)continue;
        bool found=false;const auto signature=d->atlas->catalog().gridSignatureForPacked(packed.encode(),&found);
        CTerrainVirtualMaterialField field;if(!d->terrain.copyVirtualMaterialField(ETerrainTileLayer::Land,&field,false)||!found||!(signature==field.cellSignature(tx,ty)))return 87;
        cells.append(QJsonObject{{"map",left?"zone13":"bz10k"},{"x",tx},{"y",ty},{"packed",int(packed.encode())}});++changed;
    }
    painter.end();canvas.save(output+"/cross-after.png");
    const auto firstAfter=payloads(*first),secondAfter=payloads(*second);
    if(firstBefore==firstAfter||secondBefore==secondAfter)return 88;
    const auto firstTextureAfter=textureHash(view.m_pLand),secondTextureAfter=textureHash(loaded.landscape.data());
    const auto firstAppearance=appearance(*first),secondAppearance=appearance(*second);
    if(shadowPixels(view.m_pLand)!=firstShadowBefore || shadowPixels(loaded.landscape.data())!=secondShadowBefore)return 116;
    if(!view.worldTerrainHistory(false,true)||payloads(*view.m_worldTerrainSession.documents[id])!=firstBefore||payloads(*view.m_worldTerrainSession.documents[otherId])!=secondBefore)return 89;
    if(textureHash(view.m_pLand)!=firstTextureBefore || textureHash(loaded.landscape.data())!=secondTextureBefore)return 94;
    if(!view.worldTerrainHistory(true,true)||payloads(*view.m_worldTerrainSession.documents[id])!=firstAfter||payloads(*view.m_worldTerrainSession.documents[otherId])!=secondAfter)return 90;
    if(textureHash(view.m_pLand)!=firstTextureAfter || textureHash(loaded.landscape.data())!=secondTextureAfter)return 95;
    // Compare the production sector VBOs/UVs and atlas in a deterministic FBO.
    // A small diagnostic shader excludes GUI overlays and asynchronous shadows.
    const auto geometryFrame=[&]() {
        view.makeCurrent();QOpenGLFramebufferObject fbo(640,480,QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLShaderProgram shader;
        if(!shader.addShaderFromSourceCode(QOpenGLShader::Vertex,
            "#version 120\nattribute vec3 a_position;attribute vec3 a_normal;attribute vec2 a_texture;uniform mat4 u_modelMmatrix;uniform mat4 projection;varying vec2 uv;varying float shade;void main(){uv=a_texture;shade=0.8+0.2*abs(a_normal.z);gl_Position=projection*u_modelMmatrix*vec4(a_position,1.0);}")
           ||!shader.addShaderFromSourceCode(QOpenGLShader::Fragment,
            "#version 120\nuniform sampler2D qt_Texture0;varying vec2 uv;varying float shade;void main(){gl_FragColor=vec4(texture2D(qt_Texture0,uv).rgb*shade,1.0);}")
           ||!shader.link()||!fbo.bind()||!shader.bind())return QImage();
        auto* gl=QOpenGLContext::currentContext()->functions();
        gl->glViewport(0,0,640,480);gl->glDisable(GL_SCISSOR_TEST);gl->glDisable(GL_BLEND);gl->glDisable(GL_CULL_FACE);
        gl->glEnable(GL_DEPTH_TEST);gl->glDepthMask(GL_TRUE);gl->glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        gl->glClearColor(0,0,0,1);gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        QMatrix4x4 projection;projection.ortho(float(edge*2-20),float(edge*2+20),float(cy*2-15),float(cy*2+15),-10000,10000);
        shader.setUniformValue("projection",projection);
        view.m_pLand->draw(&shader);loaded.landscape->draw(&shader);
        const auto result=fbo.toImage();shader.release();fbo.release();return result;
    };
    const QImage incrementalFrame=geometryFrame();
    QSet<QRgb> colors;
    for(int y=0;y<incrementalFrame.height();y+=2)for(int x=0;x<incrementalFrame.width();x+=2)colors.insert(incrementalFrame.pixel(x,y));
    incrementalFrame.save(output+"/cross-render.png");
    if(incrementalFrame.isNull() || colors.size()<64){out<<"FRAME colors="<<colors.size()<<"\n";return 103;}
    // Rebuild both renderers through the original full path as an independent
    // GPU oracle. All mip levels must match the optimized publication exactly.
    for(const auto& d:{first,second}) {
        auto* land=d==first?view.m_pLand:loaded.landscape.data();view.makeCurrent();
        const auto before=textureHash(land);
        if(!land->previewTerrainAtlas(d->atlas->activeTileImages(),d->atlas->activeTextureCount()))return 96;
        if(textureHash(land)!=before)return 97;
        for(const auto& sector:d->baseline.sectors())
            if(!land->previewCanonicalSector(sector.sectorX,sector.sectorY,d->terrain.sectorPayload(sector.sectorX,sector.sectorY)))return 104;
    }
    view.m_pLand->invalidateTerrainShadowField();loaded.landscape->invalidateTerrainShadowField();
    if(shadowPixels(view.m_pLand)!=firstShadowBefore || shadowPixels(loaded.landscape.data())!=secondShadowBefore)return 117;
    const auto fullFrame=geometryFrame();fullFrame.save(output+"/cross-full-render.png");
    if(fullFrame!=incrementalFrame)return 105;
#ifdef EIPUB_CANDIDATE
    // A detached but identical image is a no-op; changed same-layout pixels
    // take the in-place path. Compare mipmaps against the full-upload oracle.
    view.makeCurrent();auto* land=loaded.landscape.data();
    auto images=second->atlas->activeTileImages();const auto original=images;
    const auto texId=land->glTexture()->textureId();images[0]=images[0].copy();
    if(!land->previewTerrainAtlasIfChanged(images,images.size()/64) || land->glTexture()->textureId()!=texId)return 98;
    images[0].fill(QColor(13,57,109,255));
    if(!land->previewTerrainAtlasIfChanged(images,images.size()/64) || land->glTexture()->textureId()!=texId)return 99;
    const auto incremental=textureHash(land);
    if(!land->previewTerrainAtlas(images,images.size()/64) || textureHash(land)!=incremental)return 100;
    if(!land->previewTerrainAtlasIfChanged(original,original.size()/64) || textureHash(land)!=secondTextureAfter)return 101;
    land->clearPreviewTerrainAtlas();
    if(!land->previewTerrainAtlasIfChanged(original,original.size()/64) || textureHash(land)!=secondTextureAfter)return 102;
#endif
    // The same common brush wholly inside the active map must leave its
    // neighbor intact, including after navigation through both history entries.
    const auto localBefore=payloads(*first),neighborBefore=payloads(*second);
    const auto localAtlasBefore=textureHash(view.m_pLand);
    view.beginTileBrushGroup();view.m_tileStrokeDistributionSeed=987654321u;
    view.m_workingTerrainAtlas->setDistributionSeed(view.m_tileStrokeDistributionSeed);
    QJsonArray localStampMs;
    for(int dy=-3;dy<=3;++dy) {
        stampTimer.restart();
        if(!view.paintWorldTerrainAt(QVector3D((edge-3)*2,(cy+dy)*2,0),true))return 106;
        localStampMs.append(stampTimer.elapsed());
    }
    view.endTileBrushGroup();
    const auto localAfter=payloads(*first);const auto localAtlasAfter=textureHash(view.m_pLand);
    if(localAfter==localBefore || payloads(*second)!=neighborBefore)return 107;
    if(!view.worldTerrainHistory(false,true) || payloads(*first)!=localBefore || payloads(*second)!=neighborBefore
       || textureHash(view.m_pLand)!=localAtlasBefore)return 108;
    if(!view.worldTerrainHistory(true,true) || payloads(*first)!=localAfter || payloads(*second)!=neighborBefore
       || textureHash(view.m_pLand)!=localAtlasAfter)return 109;
#ifdef EIPUB_CANDIDATE
    // Reopening a background map discards its GPU state, but keeps history.
    // Undo must compare against that renderer's baseline, not the old command.
    view.makeCurrent();
    loaded.landscape=CLandscape::createReadOnlyBackground();
    if(!loaded.landscape->readBackgroundMap(QFileInfo(otherPath)))return 111;
    loaded.landscape->setWorldOffset(QVector3D(edge*2,0,0));
    view.m_neighborLandscapes[0].landscape=loaded.landscape;
    view.m_neighborLandscapes[0].terrainPreviewAtlas.clear();
    view.m_neighborLandscapes[0].terrainPreviewPayloads.clear();
    if(!view.worldTerrainHistory(false,true)||!view.worldTerrainHistory(false,true)
       ||payloads(*first)!=firstBefore||payloads(*second)!=secondBefore
       ||textureHash(loaded.landscape.data())!=secondTextureBefore)return 112;
    if(!view.worldTerrainHistory(true,true)||!view.worldTerrainHistory(true,true)
       ||payloads(*first)!=localAfter||payloads(*second)!=neighborBefore
       ||textureHash(loaded.landscape.data())!=secondTextureAfter)return 113;
#endif
    const auto localAppearance=appearance(*first);
    QCryptographicHash canonicalHash(QCryptographicHash::Sha256);
    for(const auto& p:localAfter)canonicalHash.addData(p);
    QFile receipt(output+"/cross-success.json");if(!receipt.open(QIODevice::WriteOnly))return 91;
    receipt.write(QJsonDocument(QJsonObject{{"result","PASS"},{"firstAppearance",QString::fromLatin1(firstAppearance)},{"secondAppearance",QString::fromLatin1(secondAppearance)},{"localAppearance",QString::fromLatin1(localAppearance)},{"maps",2},{"cells",cells},{"undoRedo",true},{"elapsedMs",timer.elapsed()},{"brushMs",brushMs},{"shadowInvalidations",int(shadowInvalidations)},{"stampMs",stampMs},{"textureFirst",QString::fromLatin1(firstTextureAfter)},{"textureSecond",QString::fromLatin1(secondTextureAfter)},{"localStampMs",localStampMs},{"localPayloadSha256",QString::fromLatin1(canonicalHash.result().toHex())},{"localTexture",QString::fromLatin1(localAtlasAfter)},{"renderFullEquivalent",true},{"shadowFullEquivalent",true},{"shadowPixelsFirst",QString::fromLatin1(firstShadowBefore)},{"shadowPixelsSecond",QString::fromLatin1(secondShadowBefore)}}).toJson());
    out<<"PASS two maps with different atlases, "<<changed<<" border cells, exact shared Undo/Redo, "<<timer.elapsed()<<" ms\n";out.flush();return 0;
}
