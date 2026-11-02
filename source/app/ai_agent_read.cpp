#include "ai_agent_read.h"
#include "main_window.h"
#include "settings.h"
#include "view.h"
#include "mob.h"
#include "landscape.h"
#include "profile_database.h"
#include "resource_profile.h"
#include "topdown_export.h"
#include "ai_agent_edit.h"
#include <QBuffer>
#include <cstdio>
#include <cmath>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ai {
QJsonObject databaseSnapshot(const QSharedPointer<const ei::SProfileDatabaseSnapshot>& db,
                            const QJsonObject& a, const Cancellation& cancel)
{
    if(!db)return error("No profile database loaded");
    QJsonArray rows;int index=0;const auto category=a["category"].toString("all");
    const auto add=[&](QString kind,QString key,QString name,QString detail) {
        if(!(key+" "+name+" "+detail).contains(a["query"].toString(),Qt::CaseInsensitive))return;
        if(index++<a["offset"].toInt()||rows.size()>=a["limit"].toInt(100))return;
        QJsonObject row{{"category",kind},{"key",key.left(256)},{"name",name.left(256)},{"detail",detail.left(256)}};
        if(kind=="prototypes"&&key.startsWith("static:")){
            row["source"]="static-model-catalog";row["nodeType"]=3;
            row["textures"]=QJsonArray::fromStringList(db->raceModels.value(key).primaryTextures);
        }
        rows.append(row);
    };
    if(category=="all"||category=="monsters")for(auto i=db->monsters.begin();i!=db->monsters.end();++i) {
        if(cancel->load())return error("Cancelled");add("monsters",i.key(),i->name,i->baseRace);
    }
    if(category=="all"||category=="items")for(auto i=db->items.begin();i!=db->items.end();++i) {
        if(cancel->load())return error("Cancelled");add("items",i.key(),i->name,i->category+" "+i->type);
    }
    if(category=="all"||category=="prototypes")for(auto i=db->raceModels.begin();i!=db->raceModels.end();++i) {
        if(cancel->load())return error("Cancelled");add("prototypes",i.key(),i->name,i->model);
    }
    QJsonObject out{{"records",rows},{"totalMatches",index},{"profile",db->profileId}};
    if(a["offset"].toInt()+rows.size()<index)out.insert("nextOffset",a["offset"].toInt()+rows.size());
    return out;
}
QJsonObject terrainSnapshot(const CTerrainEditModel& m, const QJsonObject& a, const Cancellation& cancel)
{
    const int nx=m.vertexCountX(), ny=m.vertexCountY();
    if(nx<=0||ny<=0) return error("No terrain loaded");
    double sum=0;quint16 lo=65535,hi=0;
    for(int y=0;y<ny;++y) {
        if(cancel->load()) return error("Cancelled");
        for(int x=0;x<nx;++x) {auto h=m.height(x,y);sum+=h;lo=qMin(lo,h);hi=qMax(hi,h);}
    }
    const int ox=a["x"].toInt(),oy=a["y"].toInt(),step=a["step"].toInt(1);
    QJsonArray samples;
    for(int y=0;y<a["height"].toInt(16)&&oy+y*step<ny;++y)
        for(int x=0;x<a["width"].toInt(16)&&ox+x*step<nx;++x) {
            auto pos=m.terrainVertexPosition(ox+x*step,oy+y*step);
            samples.append(QJsonObject{{"x",ox+x*step},{"y",oy+y*step},{"worldX",pos.x()},
                {"worldY",pos.y()},{"raw",m.height(ox+x*step,oy+y*step)}});
        }
    const double scale=m.maxZ()/65535.0;
    return {{"verticesX",nx},{"verticesY",ny},{"maxZ",m.maxZ()},{"rawToWorld",scale},
        {"statistics",QJsonObject{{"scope","whole terrain"},{"count",double(nx)*ny},
            {"minimum",lo*scale},{"maximum",hi*scale},{"mean",sum/(double(nx)*ny)*scale}}},
        {"samples",samples},{"sampleLimit",MaxSamples},{"source","in-memory snapshot at request time"}};
}
QJsonObject paintSnapshot(const CTerrainEditModel& m,const QJsonObject& a)
{
    if(m.vertexCountX()<=0) return error("No terrain loaded");
    const int ox=a["x"].toInt(),oy=a["y"].toInt(),step=a["step"].toInt(1);
    const auto layer=a["layer"].toString()=="overlay"?ETerrainTileLayer::Overlay:ETerrainTileLayer::Land;
    QJsonArray cells;
    for(int y=0;y<a["height"].toInt(16)&&oy+y*step<m.tileCountY();++y)
        for(int x=0;x<a["width"].toInt(16)&&ox+x*step<m.tileCountX();++x) {
            const int tx=ox+x*step,ty=oy+y*step;bool available=false,known=false;
            const int packed=m.packedTile(tx,ty,layer,&available);
            const auto type=m.tileType(quint16(packed),&known);
            QJsonObject cell{{"x",tx},{"y",ty},{"available",available},{"packed",packed}};
            if(known) cell.insert("runtimeType",type);
            if(layer==ETerrainTileLayer::Overlay) cell.insert("overlayMaterial",m.overlayMaterialIndex(tx,ty));
            if(m.virtualMaterialFieldActive(layer)) {
                QJsonArray corners;
                for(const auto& p:QVector<QPoint>{{tx,ty},{tx+1,ty},{tx+1,ty+1},{tx,ty+1}})
                    corners.append(m.virtualMaterialAt(layer,p.x(),p.y()));
                cell.insert("semanticCornersNW_NE_SE_SW",corners);
            }
            cells.append(cell);
        }
    return {{"tilesX",m.tileCountX()},{"tilesY",m.tileCountY()},{"worldUnitsPerTile",2},
        {"layer",layer==ETerrainTileLayer::Land?"land":"overlay"},{"cells",cells}};
}
static QByteArray boundedFile(const QString& path)
{
    QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>MaxMapBytes) return {};
    return f.read(MaxMapBytes+1);
}
static QJsonObject profileRead(const QJsonObject& request)
{
    const auto args=request["arguments"].toObject();
    const auto roots=request["roots"].toArray();
    const QString wanted=args["map"].toString();
    if(QDir::isAbsolutePath(wanted)||wanted.contains("..")||wanted.contains(':')) return error("Use a relative catalog map key");
    QJsonArray entries;QString found;int index=0,scanned=0;bool truncated=false;
    for(int ri=0;ri<roots.size();++ri) {
        const QDir root(roots[ri].toString());const auto canonical=QFileInfo(root.path()).canonicalFilePath();
        if(canonical.isEmpty())continue;
        QDirIterator it(root.path(),{"*.mpr","*.mob"},QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()) {
            const auto path=it.next();if(++scanned>10000){truncated=true;break;}
            const auto real=QFileInfo(path).canonicalFilePath();
            if(!real.startsWith(canonical+"/",Qt::CaseInsensitive))continue;
            const QString key=QString::number(ri)+"/"+root.relativeFilePath(path);
            if(!wanted.isEmpty() && key.compare(wanted,Qt::CaseInsensitive)==0)found=real;
            if(!wanted.isEmpty())continue;
            if(!key.contains(args["query"].toString(),Qt::CaseInsensitive))continue;
            if(index++<args["offset"].toInt())continue;
            if(entries.size()>=args["limit"].toInt(100))return {{"maps",entries},{"nextOffset",index-1},{"scanLimit",10000}};
            entries.append(QJsonObject{{"map",key},{"file",QFileInfo(path).fileName()},{"bytes",double(QFileInfo(path).size())}});
        }
        if(truncated)break;
    }
    if(wanted.isEmpty())return {{"maps",entries},{"truncated",truncated},{"scanLimit",10000}};
    if(found.isEmpty())return error("Map not found inside current profile roots (scan limit 10000)");
    const auto bytes=boundedFile(found);if(bytes.isEmpty()||bytes.size()>MaxMapBytes)return error("Unreadable map or 64 MiB file limit");
    QJsonObject result{{"map",wanted},{"source","disk, read only; unsaved editor changes excluded"},
        {"sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}};
    QString mob=found;
    if(found.endsWith(".mpr",Qt::CaseInsensitive)) {
        CMprDocument doc;CValidationReport report;
        if(!doc.loadBytes(bytes,found,&report))return error("Invalid MPR");
        const auto info=doc.mapInfo();
        if(!info.valid||quint64(info.sectorCountX)*info.sectorCountY>4096)return error("Terrain limit 4096 sectors");
        CTerrainEditModel model;if(!model.load(doc,&report))return error("Terrain projection unavailable");
        result.insert("terrain",terrainSnapshot(model,args,std::make_shared<std::atomic_bool>(false)));
        result.insert("paint",paintSnapshot(model,args));
        QJsonArray materials;for(int i=0;i<info.materials.size()&&i<MaxPage;++i)
            materials.append(QJsonObject{{"index",i},{"type",info.materials[i].type}});
        result.insert("materials",materials);result.insert("materialCount",info.materials.size());
        mob=QFileInfo(found).dir().filePath(QFileInfo(found).completeBaseName()+".mob");
        // Do not follow an adjacent MOB link outside this profile directory.
        if(!QFileInfo(mob).canonicalFilePath().startsWith(QFileInfo(found).canonicalPath()+"/",Qt::CaseInsensitive))mob.clear();
    }
    if(!mob.isEmpty()&&QFileInfo::exists(mob)) {
        CMobDocument doc;const auto data=mob==found?bytes:boundedFile(mob);
        if(data.isEmpty()||!doc.loadBytes(data,mob))return error("Invalid MOB or 64 MiB file limit");
        QJsonArray objects;const auto& all=doc.objects();
        for(int i=args["offset"].toInt();i<all.size()&&objects.size()<args["limit"].toInt(100);++i) {
            const auto& o=all[i];objects.append(QJsonObject{{"id",double(o.nid)},{"name",o.name.left(256)},
                {"prototype",o.prototype.left(256)},{"model",o.model.left(256)},
                {"position",QJsonArray{o.positionX,o.positionY,o.positionZ}},{"type",double(o.objectType)}});
        }
        result.insert("objects",objects);result.insert("objectCount",all.size());
        if(args["offset"].toInt()+objects.size()<all.size())result.insert("nextOffset",args["offset"].toInt()+objects.size());
    }
    return result;
}
int readWorkerMain(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
#ifdef Q_OS_WIN
    // A corrupt/private map cannot grow an unbounded parser heap in the GUI.
    // This handle intentionally lives until worker process termination.
    HANDLE limits=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
    policy.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    policy.ProcessMemoryLimit=512u*1024u*1024u;
    if(!limits||!SetInformationJobObject(limits,JobObjectExtendedLimitInformation,&policy,sizeof(policy))
       ||!AssignProcessToJobObject(limits,GetCurrentProcess())) {
        const QByteArray failure=QJsonDocument(error("Cannot establish read worker memory limit")).toJson(QJsonDocument::Compact);
        fwrite(failure.constData(),1,size_t(failure.size()),stdout);fflush(stdout);return 1;
    }
#endif
    QFile input;input.open(stdin,QIODevice::ReadOnly);
    QByteArray data=input.readLine(16385);
    QJsonObject result;
    if(data.size()>16384)result=error("Worker request too large");
    else result=profileRead(QJsonDocument::fromJson(data).object());
    QByteArray out=QJsonDocument(result).toJson(QJsonDocument::Compact);
    if(out.size()>MaxResult)out=QJsonDocument(error("Result limit; request a smaller region/page")).toJson(QJsonDocument::Compact);
    fwrite(out.constData(),1,size_t(out.size()),stdout);fputc('\n',stdout);fflush(stdout);return 0;
}
}

QStringList CAiReadAdapter::mapRoots(MainWindow* w) { return w->activeMapRoots().toList(); }
CTerrainEditModel CAiReadAdapter::terrain(MainWindow* w) { return w->view()->m_terrainEdit; }
QJsonObject CAiReadAdapter::context(MainWindow* w)
{
    auto* v=w->view();const auto profile=ei::ProfileDatabaseContext::instance().current();
    const auto& model=v->m_terrainEdit;QJsonArray mobs,neighbors;
    for(auto* mob:v->mobs())mobs.append(QJsonObject{{"path",mob->filePath().absoluteFilePath()},{"dirty",mob->isDirty()},{"active",v->currentMob()==mob}});
    for(const auto& n:v->m_neighborPlacements) {
        if(neighbors.size()>=ai::MaxPage)break;
        neighbors.append(QJsonObject{{"zone",n.zoneId},{"mpr",n.mprPath},{"mob",n.mobPath},
            {"origin",QJsonArray{n.worldOrigin.x(),n.worldOrigin.y()}},{"size",QJsonArray{n.size.width(),n.size.height()}}});
    }
    const auto* profileOption=dynamic_cast<COptString*>(w->m_settings->opt(eOptSetResource,"resourceProfile"));
    return {{"version",ENDURANCE_VERSION},{"mode","read-only"},{"profile",profileOption?profileOption->value():(profile?profile->profileId:QString())},
        {"mpr",CLandscape::getInstance()->isMprLoad()?CLandscape::getInstance()->filePath().absoluteFilePath():QString()},{"mobs",mobs},
        {"loadingAddMobs",w->backgroundMobLoading()},{"neighborLoadStage",v->property("neighborLoadPhase").toInt()},
        {"terrainDirty",model.isDirty()},{"worldTerrainDirty",v->hasDirtyWorldTerrain()},
        {"travelDirty",w->hasTravelChanges()},{"verticesX",model.vertexCountX()},{"verticesY",model.vertexCountY()},
        {"sizeWorld",QJsonArray{qMax(0,model.vertexCountX()-1),qMax(0,model.vertexCountY()-1)}},
        {"worldOrigin",QJsonArray{v->m_activeMapWorldOrigin.x(),v->m_activeMapWorldOrigin.y()}},{"neighbors",neighbors}};
}
QJsonObject CAiReadAdapter::read(MainWindow* w,const QString& method,const QJsonObject& a)
{
    auto* v=w->view();
    if(method=="get_context")return context(w);
    if(method=="read_paint")return ai::paintSnapshot(v->m_terrainEdit,a);
    if(method=="material_palette") {
        QJsonArray rows;const auto& global=v->m_terrainGlobalFamilies;
        const auto& local=v->m_terrainAtlasProfile.families;
        const int total=global.isEmpty()?local.size():global.size();
        int i=0;for(;i<total;++i) {
            if(i<a["offset"].toInt())continue;
            if(rows.size()>=a["limit"].toInt(100))break;
            if(global.isEmpty()) {
                const auto& m=local[i];
                if(!m.name.contains(a["query"].toString(),Qt::CaseInsensitive))continue;
                rows.append(QJsonObject{{"id",m.id},{"name",m.name.left(256)},{"runtimeType",m.tileType},{"scope","active atlas"}});
            } else {
                const auto& m=global[i];
                if(!m.name.contains(a["query"].toString(),Qt::CaseInsensitive))continue;
                rows.append(QJsonObject{{"id",m.id},{"name",m.name.left(256)},{"runtimeType",m.runtimeTerrainType},{"scope","profile palette"}});
            }
        }
        QJsonObject out{{"materials",rows},{"total",total}};if(i<total)out.insert("nextOffset",i);return out;
    }
    if(method=="list_objects") {
        QJsonArray rows;int index=0,scanned=0;bool more=false;
        for(auto* mob:v->mobs()) { if(more)break; for(auto* n:mob->nodes()) {
            if(index++<a["offset"].toInt())continue;
            if(rows.size()>=a["limit"].toInt(100)||++scanned>10000){more=true;break;}
            if(n->isMarkDeleted()||(a["selectedOnly"].toBool()&&n->nodeState()!=eSelect))continue;
            if(a.contains("type")&&int(n->nodeType())!=a["type"].toInt())continue;
            if(!(n->mapName()+" "+n->modelName()).contains(a["query"].toString(),Qt::CaseInsensitive))continue;
            const auto p=n->position();rows.append(QJsonObject{{"id",double(n->mapId())},{"name",n->mapName().left(256)},
                {"model",n->modelName().left(256)},{"type",int(n->nodeType())},{"selected",n->nodeState()==eSelect},
                {"mob",mob->filePath().fileName()},{"position",QJsonArray{p.x(),p.y(),p.z()}}});
        }}
        QJsonObject out{{"objects",rows},{"scanned",scanned}};if(more)out.insert("nextOffset",index-1);return out;
    }
    if(method=="window_snapshot") {
        // Optional post-edit snapshot uses the same fresh owning-context path.
        // Public window_snapshot is dispatched as an asynchronous server job.
        QString error;auto image=v->captureTopdownPass(ETopdownPass::None,v->size().scaled(1024,768,Qt::KeepAspectRatio),{},&error);
        if(image.isNull())return {{"code","RENDER_UNAVAILABLE"},{"error",error}};
        auto result=ai::encodeImageBounded(image,QSize(1024,768),std::make_shared<std::atomic_bool>(false));
        const auto c=context(w);result["profile"]=c["profile"];result["map"]=c["mpr"];result["capturedRevision"]=CAiEditAdapter::revision(w);return result;
    }
    return ai::error("Unknown read tool");
}
