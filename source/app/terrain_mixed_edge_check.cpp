#include "terrain_mixed_edge_check.h"
#include "terrain_mixed_edge.h"
#include "terrain_atlas_profile.h"
#include "terrain_semantic_field_store.h"
#include "mpr_document.h"
#include "res_archive_document.h"
#include "endurance_document.h"
#include <QDataStream>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QTextStream>
#include <QPainter>
#include <QTransform>
#include <QtEndian>
#include "landscape_tools.h"
#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "tree_view.h"
#include <QTableWidget>
#include <QLineEdit>
#include <QUndoStack>
#include <QApplication>
#include <QTimer>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QSettings>

namespace {
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
bool write(const QString& path,const QByteArray& bytes) { QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(); }
STerrainTransitionSignature sig(qint32 a,qint32 b,qint32 c,qint32 d)
{ STerrainTransitionSignature s;s.northWest=a;s.northEast=b;s.southEast=c;s.southWest=d;return s; }
QByteArray syntheticMpr()
{
    QByteArray mp;QDataStream out(&mp,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out<<quint32(0xCE4AF672)<<float(100)<<quint32(1)<<quint32(1)<<quint32(64)
       <<quint32(64)<<quint32(4)<<quint32(8)<<quint16(0)<<quint32(0);
    for(int i=0;i<64;++i)out<<qint32(i<4?i+1:1);
    QByteArray sector;QDataStream sec(&sector,QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);sec<<quint32(0xCF4BF774)<<quint8(0);
    sector.append(QByteArray(33*33*8+16*16*2,'\0'));
    QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019CE23C,reinterpret_cast<uchar*>(empty.data()));
    qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
    CResArchiveDocument archive;QStringList errors;QByteArray first,result;
    if(!archive.loadBytes(empty,"empty",&errors)
       ||!archive.buildWithAppendedMember("Mixed.MP",mp,0,&first,&errors)
       ||!archive.loadBytes(first,"first",&errors)
       ||!archive.buildWithAppendedMember("Mixed000000.SEC",sector,0,&result,&errors))return {};
    return result;
}
QImage preview(const CTerrainEditModel& model,const QVector<QImage>& images,const QRect& region)
{
    const int size=images.first().width();QImage result(region.width()*size,region.height()*size,QImage::Format_RGB32);
    result.fill(Qt::magenta);QPainter painter(&result);
    for(int y=region.top();y<=region.bottom();++y)for(int x=region.left();x<=region.right();++x) {
        bool found=false;const auto packed=SMprPackedTile::decode(model.packedTile(x,y,ETerrainTileLayer::Land,&found));
        if(found && packed.atlasSlot()<images.size())painter.drawImage((x-region.x())*size,(region.bottom()-y)*size,
            images[packed.atlasSlot()].transformed(QTransform().rotate(-90*packed.rotation)));
    }
    return result;
}
}

int runTerrainMixedEdgeCheck(const QString& fixturePath)
{
    QTextStream log(stdout);int passed=0,failed=0;
    auto check=[&](bool condition,const QString& name) {log<<(condition?"PASS ":"FAIL ")<<name<<'\n';log.flush();condition?++passed:++failed;return condition;};
    const QJsonObject fixture=QJsonDocument::fromJson(read(fixturePath)).object();
    const QString output=fixture.value("output").toString();
    const bool real=fixture.value("real").toBool();
    if(!check(!output.isEmpty() && QDir(output).exists(),"isolated fixture directory"))return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,output+"/settings");
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,output+"/settings");
    QSettings().remove(QStringLiteral("ui/landscape/modernMixedEdge"));
    {
        CLandscapeTools panel;panel.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
        auto option=panel.findChild<QCheckBox*>("modernMixedEdge");
        check(option && !option->isChecked()&&!panel.modernMixedEdge(),"mixed-edge defaults off");
        option->setChecked(true);
        CLandscapeTools restored;restored.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
        check(restored.modernMixedEdge(),"mixed-edge preference persisted");
        restored.setPaintPaletteMode(ETerrainPaintPaletteMode::ClassicAuto);
        check(!restored.modernMixedEdge(),"Classic ignores mixed-edge");
        restored.setPaintPaletteMode(ETerrainPaintPaletteMode::Manual);
        check(!restored.modernMixedEdge(),"Manual ignores mixed-edge");
        for(int usedSlots:{2048,2049,4096,4097}) {
            panel.setPaintMaterials({}, {}, {},usedSlots,usedSlots);panel.setGeneratedAtlasTileCount(0);
            auto budget=panel.findChild<QProgressBar*>("terrainAtlasCapacity");
            const QString color=usedSlots<=2048?"#35a853":usedSlots<=4096?"#ebbe23":"#cd3737";
            check(budget && budget->maximum()==4096 && budget->styleSheet().contains(color),QStringLiteral("budget threshold %1, default limit4096").arg(usedSlots));
        }
        panel.setPaintMaterials({}, {}, {},1216,1216);
        panel.setGeneratedAtlasTileCount(55,1217);
        check(panel.findChild<QProgressBar*>("terrainAtlasCapacity")->format().startsWith("1217 / 4096"),
            "mixed-edge budget uses actual logical count across partial authored page");
        option->setChecked(false);
    }
    CMprDocument document;CValidationReport report;STerrainAtlasProfile profile;
    QVector<QImage> images;QVector<qint32> types;CTerrainTransitionCatalog catalog;QMap<qint32,qint32> materialTypes;
    QVector<qint32> baseline;QByteArray sourceBytes;
    if(real)
    {
        sourceBytes=read(fixture.value("mpr").toString());
        const auto atlasObject=QJsonDocument::fromJson(read(fixture.value("atlas").toString())).object();
        if(!check(STerrainAtlasProfile::fromJson(atlasObject["catalog"].toObject(),&profile,&report),"Zone6 exact profile load"))return 1;
        catalog=profile.usableAnalysis(&report).overlayCatalogWithLandFamilies();
        for(const auto& family:profile.families)materialTypes.insert(family.id,family.tileType);
        for(int slot=0;slot<profile.atlasSlotCount;++slot)images.append(QImage(fixture.value("tiles").toString()+QStringLiteral("/%1.png").arg(slot)));
        const auto mapObject=QJsonDocument::fromJson(read(fixture.value("map").toString())).object();
        for(const auto& value:mapObject["field"].toArray())baseline.append(value.toInt());
    }
    else
    {
        sourceBytes=syntheticMpr();
        const QRgb colors[]={qRgb(180,40,30),qRgb(30,190,50),qRgb(40,50,200),qRgb(210,180,30)};
        for(int i=0;i<64;++i){QImage image(128,128,QImage::Format_RGBA8888);image.fill(QColor::fromRgba(colors[i<4?i:0]));images.append(image);}
        auto add=[&](int slot,const STerrainTransitionSignature& s){STerrainTransitionVariant v;v.signature=s;SMprPackedTile p;p.tileIndex=quint8(slot);v.packedTile=p.encode();catalog.addVariant(v);};
        for(int i=0;i<4;++i){add(i,sig(i+1,i+1,i+1,i+1));materialTypes.insert(i+1,i+1);}
        int slot=4;
        for(int a=1;a<=4;++a)for(int b=a+1;b<=4;++b,++slot) {
            for(int y=0;y<128;++y)for(int x=0;x<128;++x)images[slot].setPixelColor(x,y,QColor::fromRgba(colors[x<64?a-1:b-1]));
            add(slot,sig(a,b,b,a));
        }
        for(int i=slot;i<64;++i)add(i,sig(1,1,1,1));
    }
    if(!check(document.loadBytes(sourceBytes,fixture.value("mpr").toString(),&report),"MPR load"))return 1;
    types=document.mapInfo().tileTypes;
    if(types.size()<images.size())types.resize(images.size());
    CTerrainEditModel model;
    if(!check(model.load(document,&report),"edit model load"))return 1;
    model.enableWorkingAtlasSlots(4096);
    CTerrainWorkingAtlas atlas(catalog,images,17,4096,{}, {},types,materialTypes);
    if(!check(atlas.isValid(),"working atlas valid: "+atlas.error()))return 1;
    const int w=model.tileCountX(),h=model.tileCountY(),vw=w+1;
    if(!real)baseline.fill(1,vw*(h+1));
    if(!check(model.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,baseline,baseline,&report),"authoritative vertex field restored"))return 1;
    const int patchX=fixture.value("patchX").toInt(113),patchY=fixture.value("patchY").toInt(99);
    QRect region(real?patchX-4:2,real?patchY-4:2,real?16:12,real?16:12);
    QVector<qint32> current=baseline;
    qint32 paint=2;
    // Real workload uses a measured 6x6 uniform vertex patch in the original
    // Zone6 field (113,99), so only the actual new brush ring is rebuilt.
    if(real)
    {
        paint=fixture.value("paintMaterial").toInt(1974896831);
        const qint32 base=fixture.value("baseMaterial").toInt(2142727678);
        bool patch=patchX>=0 && patchY>=0 && patchX+6<=vw && patchY+6<=h+1;
        if(patch) for(int y=patchY;y<patchY+6;++y)for(int x=patchX;x<patchX+6;++x)
            patch&=current[y*vw+x]==base;
        if(!check(patch,"original Zone6 6x6 patch verified, no undercoat"))return 1;
    }
    const auto beforeStroke=model;auto beforeAtlas=atlas.createStagingCopy();
    QVector<qint32> beforeField,beforeBaseline;
    beforeStroke.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&beforeField,&beforeBaseline);
    if(!check(preview(model,atlas.activeTileImages(),region).save(output+"/before.png"),"before PNG"))return 1;
    if(real && fixture.contains("nativeX") && fixture.contains("nativeY"))
        check(preview(model,atlas.activeTileImages(),QRect(fixture["nativeX"].toInt()-2,
            fixture["nativeY"].toInt()-2,5,5)).save(output+"/native5x5.png"),"native same-pair 5x5 PNG");
    STerrainTileCommand stroke;stroke.description="Mixed-edge stroke";stroke.semanticAudit.valid=true;
    stroke.semanticAudit.layer=ETerrainTileLayer::Land;stroke.semanticAudit.requestedMaterialId=paint;
    const int cx=region.x()+5,cy=region.y()+5;
    for(int y=cy;y<=cy+3;++y)for(int x=cx;x<=cx+3;++x) {
        STerrainTileCommand::SVirtualVertexDelta d;d.globalX=x;d.globalY=y;d.before=current[y*vw+x];d.after=paint;
        if(!real && x==cx+1 && y==cy+1)d.after=3;
        if(!real && x==cx && y==cy+1)d.after=4;
        if(!real && x==cx && y==cy)d.after=1;
        stroke.virtualVertices.append(d);stroke.semanticAudit.paintedVertices.append(QPoint(x,y));current[y*vw+x]=d.after;
    }
    if(!check(model.apply(stroke,true,&report),"semantic stroke applied"))return 1;
    QSet<int> affected;for(const auto& v:stroke.virtualVertices)for(int y:{v.globalY-1,v.globalY})for(int x:{v.globalX-1,v.globalX})affected.insert(y*w+x);
    const auto semanticStroke=stroke;
    auto staged=atlas.createStagingCopy();CValidationReport bakeReport;
    auto resolution=CTerrainMixedEdge::resolve(model,staged.data(),affected,materialTypes,{},QPoint(),paint,17,false,&bakeReport);
    if(!check(!bakeReport.hasErrors()&&!resolution.deltas.isEmpty(),"mixed-edge physical resolution")){log<<bakeReport.toText();return 1;}
    check(write(output+"/stock-diagnostics.txt",bakeReport.toText().toUtf8()),"measured stock source diagnostics");
    const int mixedSlot=SMprPackedTile::decode(resolution.deltas.first().after).atlasSlot();
    const auto mixedSignature=resolution.deltas.first().requestedSignature;
    const QString aliasKey=QStringLiteral("mixed-edge-b2:")+QString(64,QLatin1Char('a'));
    const int aliasSlot=staged->importMixedEdgeTile(staged->tileImages()[mixedSlot],
        staged->runtimeTerrainTypeForSlot(mixedSlot),mixedSignature,aliasKey,&bakeReport);
    check(aliasSlot==mixedSlot,"same pixels/signature/runtime class reuse slot with recipe alias");
    // A saved soft B2 cache has all four old variant keys. The revised
    // algorithm must regenerate their pixels, then reuse its own crisp keys.
    auto legacy=beforeAtlas->createStagingCopy(); CValidationReport legacyReport;
    const qint32 mixedType=staged->runtimeTerrainTypeForSlot(mixedSlot);
    int legacySlot=-1;
    for(int variant=0;variant<4;++variant)
    {
        QByteArray oldKey;
        for(qint32 id:{mixedSignature.northWest,mixedSignature.northEast,mixedSignature.southEast,mixedSignature.southWest})
            oldKey+=QByteArray::number(id)+',';
        oldKey+='|'+QByteArray::number(variant)+'|'+QByteArray::number(mixedType);
        const QString recipe=QStringLiteral("mixed-edge-b2:")
            +QString::fromLatin1(QCryptographicHash::hash(oldKey,QCryptographicHash::Sha256).toHex());
        legacySlot=legacy->importMixedEdgeTile(images.first(),mixedType,mixedSignature,recipe,&legacyReport);
    }
    const auto upgraded=CTerrainMixedEdge::resolve(model,legacy.data(),affected,materialTypes,{},QPoint(),paint,17,false,&legacyReport);
    bool upgradedCache=legacySlot>=0 && !legacyReport.hasErrors() && !upgraded.deltas.isEmpty();
    for(const auto& d:upgraded.deltas) if(d.requestedSignature==mixedSignature)
        upgradedCache &= SMprPackedTile::decode(d.after).atlasSlot()!=legacySlot;
    check(upgradedCache,"saved soft variant keys do not suppress crisp regeneration");
    auto typed=staged->createStagingCopy();
    const int typedSlot=typed->importMixedEdgeTile(staged->tileImages()[mixedSlot],
        (staged->runtimeTerrainTypeForSlot(mixedSlot)+1)%16,mixedSignature,
        QStringLiteral("mixed-edge-b2:")+QString(64,QLatin1Char('b')),&bakeReport);
    check(typedSlot>=0 && typedSlot!=mixedSlot,"same pixels with different runtime class stay separate");
    auto animated=staged->createStagingCopy();SMprAnimatedTileRecord animation;
    animation.tileIndex=SMprPackedTile::decode(model.packedTile(cx,cy,ETerrainTileLayer::Land)).atlasSlot();
    animation.frameCount=1;CValidationReport animationReport;
    CTerrainMixedEdge::resolve(model,animated.data(),affected,materialTypes,{animation},QPoint(),paint,17,false,&animationReport);
    check(animationReport.hasErrors(),"animated Ground cell explicitly refused");
    check(atlas.stateFingerprint()==beforeAtlas->stateFingerprint(),"staging does not mutate live atlas");
    check(atlas.commitStaging(*staged)&&model.installWorkingAtlasSlotTypes(atlas.activeTileTypes())&&model.apply(resolution,true,&report),"atlas/classes/SEC commit");
    appendTerrainTileCommand(&stroke,resolution);stroke.workingAtlasBefore=beforeAtlas;stroke.workingAtlasAfter=staged;
    const int used=atlas.activeTileCount();
    int triple=0,quad=0;bool seamMatch=true;quint64 edges=0,channels=0;
    QSet<int> mixedCells;
    for(const auto& p:resolution.semanticAudit.affectedCells)mixedCells.insert(p.y()*w+p.x());
    CTerrainVirtualMaterialField bakedField;model.copyVirtualMaterialField(ETerrainTileLayer::Land,&bakedField,false);
    for(int cell:mixedCells) {
        const int x=cell%w,y=cell/w;
        const auto s=bakedField.cellSignature(x,y);
        QSet<qint32> ids;for(qint32 id:{s.northWest,s.northEast,s.southEast,s.southWest})ids.insert(id);
        triple+=ids.size()==3;quad+=ids.size()==4;
        const auto& a=atlas.tileImages()[SMprPackedTile::decode(model.packedTile(x,y,ETerrainTileLayer::Land)).atlasSlot()];
        if(x+1<w && mixedCells.contains(cell+1)) {
            const auto& b=atlas.tileImages()[SMprPackedTile::decode(model.packedTile(x+1,y,ETerrainTileLayer::Land)).atlasSlot()];
            ++edges;for(int t=0;t<a.width();++t){seamMatch&=a.pixel(a.width()-1,t)==b.pixel(0,t);channels+=4;}
        }
        if(y+1<h && mixedCells.contains(cell+w)) {
            const auto& b=atlas.tileImages()[SMprPackedTile::decode(model.packedTile(x,y+1,ETerrainTileLayer::Land)).atlasSlot()];
            ++edges;for(int t=0;t<a.width();++t){seamMatch&=a.pixel(t,0)==b.pixel(t,b.height()-1);channels+=4;}
        }
    }
    const auto tiedCell=SMprPackedTile::decode(model.packedTile(cx-1,cy+2,ETerrainTileLayer::Land));
    check(atlas.runtimeTerrainTypeForSlot(tiedCell.atlasSlot())==materialTypes.value(paint),
        "painted runtime class wins known two-two edge tie");
    check(seamMatch && edges>0,"integrated mixed-cell shared RGBA edges exact");
    if(!real)
    {
        bool uniformSeams=true;quint64 uniformEdges=0;
        for(int cell:mixedCells)
        {
            const int x=cell%w,y=cell/w;
            const auto& a=atlas.tileImages()[SMprPackedTile::decode(model.packedTile(x,y,ETerrainTileLayer::Land)).atlasSlot()];
            const QPoint neighbors[]={QPoint(x-1,y),QPoint(x+1,y),QPoint(x,y-1),QPoint(x,y+1)};
            for(int e=0;e<4;++e)
            {
                const auto p=neighbors[e];
                if(p.x()<0||p.y()<0||p.x()>=w||p.y()>=h||mixedCells.contains(p.y()*w+p.x()))continue;
                const auto& b=atlas.tileImages()[SMprPackedTile::decode(model.packedTile(p.x(),p.y(),ETerrainTileLayer::Land)).atlasSlot()];
                ++uniformEdges;
                for(int t=0;t<a.width();++t)
                    uniformSeams &= e==0?a.pixel(0,t)==b.pixel(b.width()-1,t)
                        :e==1?a.pixel(a.width()-1,t)==b.pixel(0,t)
                        :e==2?a.pixel(t,a.height()-1)==b.pixel(t,0)
                        :a.pixel(t,0)==b.pixel(t,b.height()-1);
            }
        }
        check(uniformSeams && uniformEdges>0,"synthetic mixed-to-unchanged-uniform shared edges exact");
        log<<"Uniform neighbor edges="<<uniformEdges<<'\n';
    }

    if(!real)check(triple>0 && quad>0,"integrated three- and four-material nodes");
    log<<"Integrated edges="<<edges<<" channelComparisons="<<channels<<" triple="<<triple<<" quad="<<quad<<'\n';
    check(preview(model,atlas.activeTileImages(),region).save(output+"/after.png"),"after PNG");
    if(real) check(preview(model,atlas.activeTileImages(),QRect(cx-2,cy-2,5,5)).save(output+"/edge5x5.png"),
        "new edge 5x5 PNG");
    QVector<qint32> afterField,afterBaseline;model.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&afterField,&afterBaseline);
    const auto afterFingerprint=atlas.stateFingerprint();
    check(model.apply(stroke,false,&report)&&atlas.commitStaging(*beforeAtlas),"one transaction Undo");
    QVector<qint32> undone,unused;model.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&undone,&unused);
    check(undone==beforeField && unused==beforeBaseline,"Undo exact current and baseline material fields");
    bool undoCells=true;for(const auto& d:stroke.deltas)undoCells&=model.packedTile(d.globalX,d.globalY,d.layer)==d.before;
    check(undoCells && atlas.stateFingerprint()==beforeAtlas->stateFingerprint(),"Undo exact SEC and atlas fingerprint");
    check(atlas.commitStaging(*staged)&&model.installWorkingAtlasSlotTypes(atlas.activeTileTypes())&&model.apply(stroke,true,&report),"one transaction Redo");
    check(atlas.stateFingerprint()==afterFingerprint,"Redo exact atlas fingerprint");
    auto repeat=atlas.createStagingCopy();CValidationReport repeatReport;
    auto repeated=CTerrainMixedEdge::resolve(model,repeat.data(),affected,materialTypes,{},QPoint(),paint,17,false,&repeatReport);
    check(!repeatReport.hasErrors()&&repeated.deltas.isEmpty()&&repeat->activeTileCount()==used,"repeat stroke reuses all physical slots");
    auto overflow=beforeAtlas->createStagingCopy();CValidationReport budgetReport;
    CTerrainMixedEdge::resolve(model,overflow.data(),affected,materialTypes,{},QPoint(),paint,17,false,&budgetReport,beforeAtlas->activeTileCount());
    check(budgetReport.hasErrors()&&atlas.stateFingerprint()==afterFingerprint,"budget refusal leaves live state intact");
    auto missing=atlas.createStagingCopy();auto badTypes=materialTypes;badTypes.remove(paint);CValidationReport missingReport;
    CTerrainMixedEdge::resolve(model,missing.data(),affected,badTypes,{},QPoint(),paint,17,false,&missingReport);
    check(missingReport.hasErrors(),"missing material source/class refused");
    if(!real)
    {
        STerrainAtlasProfile syntheticProfile;
        syntheticProfile.mprNamespace=document.mapInfo().innerNamespace;
        syntheticProfile.atlasSlotCount=images.size();
        syntheticProfile.textureCount=images.size()/64;
        syntheticProfile.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(images);
        for(auto type=materialTypes.cbegin();type!=materialTypes.cend();++type)
        {
            STerrainAtlasProfileFamily family;family.id=type.key();family.tileType=type.value();
            family.name=QString::number(type.key());syntheticProfile.families.append(family);
        }
        const QString scenePath=output+"/preview-source.mpr";
        check(write(scenePath,sourceBytes),"isolated mapper preview source");
        CSettings settings;QTableWidget properties;properties.setColumnCount(2);
        QUndoStack history;CProgressView progress;QLineEdit coordinates;CTreeView tree;CTileForm tiles;
        CLandscapeTools panel;panel.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
        panel.findChild<QCheckBox*>("modernMixedEdge")->setChecked(true);
        CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
        view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
        view.attachLandscapeTools(&panel);
        view.resize(320,240);view.show();QApplication::processEvents();view.makeCurrent();
        view.m_pLand->readMap(QFileInfo(scenePath),false);view.m_mprDocument=document;
        view.m_terrainEdit=beforeStroke;view.m_terrainEditAvailable=true;
        view.m_pLand->setCanonicalTerrainModel(&view.m_terrainEdit);
        view.m_workingTerrainAtlas=beforeAtlas->createStagingCopy();
        view.m_authoredTerrainTileImages=images;view.m_terrainAtlasProfile=syntheticProfile;
        tiles.adoptTileImages(images,types);
        view.beginTileBrushGroup();
        check(view.applyTerrainTilePreview(semanticStroke,true),"mapper semantic drag preview");
        appendTerrainTileCommand(&view.m_tileStrokeCommand,semanticStroke);
        view.endTileBrushGroup();
        check(history.count()==1 && !view.m_workingTerrainAtlas->mixedEdgeRecipes().isEmpty(),
            "mapper release installs mixed-edge and pushes one actual Undo entry");
        const auto viewAfter=view.m_workingTerrainAtlas->stateFingerprint();
        const auto viewAfterSec=view.m_terrainEdit.sectorPayload(0,0);
        QVector<qint32> viewField,viewBaseline;
        view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&viewField,&viewBaseline);
        history.undo();
        QVector<qint32> viewUndone,viewUndoneBaseline;
        view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&viewUndone,&viewUndoneBaseline);
        check(viewUndone==beforeField && viewUndoneBaseline==beforeBaseline
            && view.m_workingTerrainAtlas->stateFingerprint()==beforeAtlas->stateFingerprint()
            && view.m_terrainEdit.sectorPayload(0,0)==beforeStroke.sectorPayload(0,0),
            "actual mapper Undo restores atlas and both material fields");
        history.redo();
        QVector<qint32> viewRedone,viewRedoneBaseline;
        view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&viewRedone,&viewRedoneBaseline);
        check(viewRedone==viewField && viewRedoneBaseline==viewBaseline
            && view.m_workingTerrainAtlas->stateFingerprint()==viewAfter
            && view.m_terrainEdit.sectorPayload(0,0)==viewAfterSec,
            "actual mapper Redo restores atlas and both material fields");
        check(tiles.tileImages()==view.m_workingTerrainAtlas->activeTileImages(),
            "mapper preview picker contains the installed mixed-edge pixels");
        view.m_worldTerrainEnabled=true;view.m_worldTerrainEnabledPaths.insert("neighbor.mpr");
        auto worldFootprint=semanticStroke;CValidationReport scopeReport;
        check(!view.resolveMixedEdgeTerrain(&worldFootprint,&scopeReport) && scopeReport.hasErrors()
            && view.m_workingTerrainAtlas->stateFingerprint()==viewAfter,
            "shared neighboring-map brush explicitly refused without mutation");
        view.m_worldTerrainEnabled=false;view.m_worldTerrainEnabledPaths.clear();
        auto animatedFootprint=semanticStroke;STerrainTileDelta oldAnimated;
        oldAnimated.globalX=cx;oldAnimated.globalY=cy;oldAnimated.before=0;
        oldAnimated.after=view.m_terrainEdit.packedTile(cx,cy,ETerrainTileLayer::Land);
        animatedFootprint.deltas.append(oldAnimated);
        SMprAnimatedTileRecord oldAnimation;oldAnimation.tileIndex=0;oldAnimation.frameCount=1;
        view.m_mprAnimatedTiles={oldAnimation};CValidationReport oldAnimationReport;
        check(!view.resolveMixedEdgeTerrain(&animatedFootprint,&oldAnimationReport)
            && oldAnimationReport.hasErrors() && view.m_workingTerrainAtlas->stateFingerprint()==viewAfter,
            "original animated SEC cell refused even after uniform drag preview replaced its reference");
        view.m_mprAnimatedTiles.clear();

        check(view.confirmTerrainAtlasBudgetForSave(4096),"save threshold 4096 needs no warning");
        QTimer warningTimer;warningTimer.setInterval(10);bool sawBudgetWarning=false,defaultCancel=false;
        QObject::connect(&warningTimer,&QTimer::timeout,[&]() {
            for(auto widget:QApplication::topLevelWidgets())
                if(auto box=qobject_cast<QMessageBox*>(widget))
                    if(box->isVisible() && box->standardButtons()==(QMessageBox::Yes|QMessageBox::Cancel))
                    {
                        sawBudgetWarning=true;defaultCancel=box->defaultButton()==box->button(QMessageBox::Cancel);
                        box->button(QMessageBox::Cancel)->click();
                    }
        });warningTimer.start();
        const bool overBudgetAccepted=view.confirmTerrainAtlasBudgetForSave(4097);warningTimer.stop();
        check(!overBudgetAccepted && sawBudgetWarning && defaultCancel,
            "save above4096 has explicit warning, Cancel default, no silent save");

        view.m_pLand->setCanonicalTerrainModel(nullptr);view.m_pLand->unloadMpr();
        panel.findChild<QCheckBox*>("modernMixedEdge")->setChecked(false);
    }
    if(!real)
    {
        CTerrainTransitionCatalog onlyLocalOne;STerrainTransitionVariant one;
        one.signature=sig(1,1,1,1);one.packedTile=0;onlyLocalOne.addVariant(one);
        CTerrainWorkingAtlas foreign(onlyLocalOne,images,17,4096,{}, {},types,materialTypes);
        const auto foreignSignature=sig(2,2,2,2);
        foreign.importPhysicalTile(images[1],2,&foreignSignature,"foreign-registry",0,&report,true);
        auto foreignModel=beforeStroke;QVector<qint32> foreignField(vw*(h+1),2);
        foreignModel.installWorkingAtlasSlotTypes(foreign.activeTileTypes());
        foreignModel.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,foreignField,foreignField,&report);
        CValidationReport foreignReport;
        CTerrainMixedEdge::resolve(foreignModel,&foreign,{cy*w+cx},materialTypes,{},QPoint(),2,17,false,&foreignReport);
        check(foreignReport.hasErrors(),"uniform-only stroke refuses a foreign substitute when local uniform source is missing");
    }
    // Existing dense publisher, with source pins and exact placed cells.
    CTerrainVirtualMaterialField field;model.copyVirtualMaterialField(ETerrainTileLayer::Land,&field,false);
    QVector<quint16> pinned,placed;QVector<qint32> placedTypes;QVector<bool> preserve(w*h,true);
    for(int slot=0;slot<atlas.authoredSlotCount();++slot){SMprPackedTile p;p.atlasIndex=quint8(slot/64);p.tileIndex=quint8(slot%64);bool known=false;atlas.catalog().gridSignatureForPacked(p.encode(),&known);if(known)pinned.append(p.encode());}
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto p=model.packedTile(x,y,ETerrainTileLayer::Land);placed.append(p);placedTypes.append(atlas.runtimeTerrainTypeForSlot(SMprPackedTile::decode(p).atlasSlot()));}
    CTerrainVirtualAtlas publisher(atlas.catalog(),atlas.activeTileImages(),17,{}, {},true,atlas.activeTileTypes(),materialTypes);
    CValidationReport saveReport;
    auto finalized=publisher.finalize(field,document.mapInfo().innerNamespace,4096,atlas.activeTileTypes(),&saveReport,pinned,placedTypes,placed,{},&atlas.catalog(),preserve);
    if(!check(finalized.success,"dense atlas finalization")){log<<finalized.error<<saveReport.toText();return 1;}
    STerrainWorkingFinalizationOrigins origins;
    check(atlas.restoreFinalizedProvenance(&finalized,&origins,document.mapInfo().innerNamespace,field.stableSha256(),pinned,&saveReport),"publication restores recipe provenance");
    auto package=CTerrainVirtualAtlas::buildPortablePackage(document,model,finalized,ETerrainTileLayer::Land,&saveReport);
    if(!check(package.success,"existing MPR/RES package path")){log<<package.error<<saveReport.toText();return 1;}
    QVector<QImage> physical;check(CTerrainVirtualAtlas::decodePhysicalTileImages(finalized,&physical,&saveReport),"DXT1 physical atlas decode");
    auto portableProfile=CTerrainAtlasProfileStore::portableFinalizedProfile(finalized,physical,profile,ETerrainAtlasLayer::Land,{},materialTypes,&saveReport);
    const auto profileBytes=portableProfile.toBytes();STerrainAtlasProfile reopenedProfile;
    check(STerrainAtlasProfile::fromBytes(profileBytes,&reopenedProfile,&saveReport),"portable profile fresh reopen");
    CMprDocument candidate;check(candidate.loadBytes(package.mprBytes,"candidate.mpr",&saveReport),"MPR candidate reopen");
    STerrainSemanticFieldDocument semantics;semantics.mprNamespace=candidate.mapInfo().innerNamespace;
    semantics.vertexWidth=vw;semantics.vertexHeight=h+1;semantics.land=afterField;
    semantics.terrainTilesSha256=STerrainSemanticFieldDocument::tileBindingSha256(candidate);
    semantics.atlasPixelSha256=QByteArray::fromHex(CTerrainAtlasProfileInference::pixelSha256(physical));
    semantics.profileSha256=QCryptographicHash::hash(profileBytes,QCryptographicHash::Sha256);
    const auto semanticBytes=semantics.toBytes(&saveReport);QByteArray embedded;
    check(STerrainSemanticFieldDocument::embedInMpr(package.mprBytes,semanticBytes,&embedded,&saveReport),"semantics embedded through existing MPR member");
    check(write(output+"/saved.mpr",embedded)&&write(output+"/saved.res",package.resourceArchiveBytes)&&write(output+"/profile.json",profileBytes),"saved isolated MPR/RES/profile");
    CMprDocument reopened;CResArchiveDocument reopenedRes;QStringList errors;
    check(reopened.loadBytes(read(output+"/saved.mpr"),"saved.mpr",&saveReport)&&reopenedRes.loadBytes(read(output+"/saved.res"),"saved.res",&errors),"disk fresh reopen");
    STerrainSemanticFieldDocument restored;
    check(STerrainSemanticFieldDocument::fromBytes(reopened.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&restored,&saveReport)&&restored.land==afterField,"reopened vertex field exact");
    CTerrainEditModel loaded;check(loaded.load(reopened,&saveReport),"reopened edit model");
    bool secMatches=true;for(int y=0;y<h;++y)for(int x=0;x<w;++x)secMatches&=loaded.packedTile(x,y,ETerrainTileLayer::Land)==finalized.packedCells[y*w+x];
    check(secMatches,"all reopened SEC physical references exact");
    bool pagesMatch=true;for(int page=0;page<finalized.textureCount;++page)pagesMatch&=reopenedRes.memberPayload(QStringLiteral("%1%2.mmp").arg(finalized.isolatedNamespace).arg(page,3,10,QLatin1Char('0')))==finalized.pageMmpPayloads[page];
    check(pagesMatch,"all reopened atlas page payloads exact");
    // Cache metadata remains bound to the exact profile pixels and physical
    // slots, so re-encoding the generated recipe does not grow the atlas.
    CTerrainWorkingAtlas reopenedAtlas(reopenedProfile.usableAnalysis(&saveReport).overlayCatalogWithLandFamilies(),physical,17,4096,{}, {},finalized.tileTypes,materialTypes);
    for(const auto& def:reopenedProfile.slotDefinitions)for(const auto& key:def.mixedEdgeRecipes)reopenedAtlas.restoreMixedEdgeRecipe(def.slot,key);
    check(!reopenedAtlas.mixedEdgeRecipes().isEmpty() && reopenedAtlas.mixedEdgeRecipes().size()==atlas.mixedEdgeRecipes().size(),"every recipe alias survives publication/profile reopen");
    loaded.enableWorkingAtlasSlots(4096);loaded.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,afterField,afterField,&saveReport);
    CValidationReport reopenPaintReport;auto reopenedStage=reopenedAtlas.createStagingCopy();
    auto repaint=CTerrainMixedEdge::resolve(loaded,reopenedStage.data(),affected,materialTypes,{},QPoint(),paint,17,false,&reopenPaintReport);
    log<<"Repaint details: deltas="<<repaint.deltas.size()<<" slots="<<reopenedAtlas.activeTileCount()<<"->"<<reopenedStage->activeTileCount()<<'\n';
    for(const auto& d:repaint.deltas)log<<"repaint cell "<<d.globalX<<','<<d.globalY<<" packed "<<d.before<<"->"<<d.after<<" corners "<<d.requestedSignature.northWest<<','<<d.requestedSignature.northEast<<','<<d.requestedSignature.southEast<<','<<d.requestedSignature.southWest<<'\n';
    check(!reopenPaintReport.hasErrors()&&repaint.deltas.isEmpty()&&reopenedStage->activeTileCount()==reopenedAtlas.activeTileCount(),"reopened repeated stroke reuses slots and SEC");
    if(reopenPaintReport.hasErrors())log<<reopenPaintReport.toText();
    log<<"Mixed-edge totals: "<<passed<<" PASS / "<<failed<<" FAIL / 0 SKIP; workingSlots="<<used<<" publishedSlots="<<finalized.requiredSlots<<" cells="<<w*h<<"\n";log.flush();
    return failed?1:0;
}
