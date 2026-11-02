#include "ai_agent_edit.h"
#include "main_window.h"
#include "view.h"
#include "mob.h"
#include "node.h"
#include "worldobj.h"
#include "lever.h"
#include "landscape.h"
#include "endurance_document.h"
#include "script_reference_analyzer.h"
#include "res_archive_index.h"
#include "ai_graph_preview_invalidation.h"
#include "map_object_placement_policy.h"
#include "technical_model_catalog.h"
#include "resourcemanager.h"
#include <QtWidgets>
#include <QtEndian>
#include <cmath>
#include <algorithm>

namespace {
constexpr int ObjectLimit=500;
QJsonArray vector(const QVector3D& v){return {v.x(),v.y(),v.z()};}
QVector3D vector(const QJsonArray& a){return {float(a[0].toDouble()),float(a[1].toDouble()),float(a[2].toDouble())};}
QJsonObject world(const QJsonObject& d){return d.contains("World object")?d["World object"].toObject():d;}
QJsonObject base(const QJsonObject& d){return world(d)["Base object"].toObject();}
QJsonObject setBase(QJsonObject d,const QJsonObject& b){auto o=world(d);o["Base object"]=b;if(d.contains("World object"))d["World object"]=o;else d=o;return d;}
QJsonObject row(CMob* mob,CNode* n){const auto d=n->toJson();return {{"mob",mob->filePath().absoluteFilePath()},{"id",double(n->mapId())},
    {"type",int(n->nodeType())},{"prototype",world(d)["Object template"]},{"model",n->modelName()},
    {"name",n->mapName()},{"position",vector(n->position())},{"data",d}};}
QByteArray hashFile(const QString& p){return resFileSha256(p);}
QJsonObject finding(QString level,QString code,QString message,QString path={},QJsonValue id=QJsonValue()){
    return {{"level",level},{"code",code},{"message",message},{"path",path},{"id",id}};
}
bool inside(const QJsonObject& a,const QPointF& p){
    if(a["shape"]=="rectangle")return QRectF(a["x"].toDouble(),a["y"].toDouble(),a["width"].toDouble(),a["height"].toDouble()).contains(p);
    QPolygonF polygon;for(const auto& v:a["points"].toArray()){auto xy=v.toArray();polygon<<QPointF(xy[0].toDouble(),xy[1].toDouble());}
    // Include the boundary, as for the editor's rectangular selection.
    for(int i=0;i<polygon.size();++i){auto u=polygon[i],v=polygon[(i+1)%polygon.size()];auto e=v-u;double l=e.x()*e.x()+e.y()*e.y();
        double t=l?qBound(0.0,((p-u).x()*e.x()+(p-u).y()*e.y())/l,1.0):0;
        if(QLineF(p,u+t*e).length()<1e-6)return true;}
    return polygon.containsPoint(p,Qt::OddEvenFill);
}
bool selected(const QJsonObject& r,const QJsonObject& a){
    if(a.contains("ids")&&!a["ids"].toArray().contains(r["id"]))return false;
    if(a.contains("area")&&!inside(a["area"].toObject(),QPointF(r["position"].toArray()[0].toDouble(),r["position"].toArray()[1].toDouble())))return false;
    if(a.contains("type")&&a["type"]!=r["type"])return false;
    const auto prototype=a["prototype"].toString();
    return prototype.isEmpty()||prototype.compare(r["prototype"].toString(),Qt::CaseInsensitive)==0
        ||prototype.compare(r["model"].toString(),Qt::CaseInsensitive)==0;
}
// Parent IDs, literal script lookups, name lookups and explicit ID fields in
// unit logic. No global numeric text search: coordinates and flags are not IDs.
void logicReferences(const QJsonValue& value,const QString& path,QMap<quint32,QStringList>& refs){
    if(value.isArray()){int i=0;for(const auto& v:value.toArray())logicReferences(v,path+"/"+QString::number(i++),refs);}
    else if(value.isObject()){auto o=value.toObject();for(auto i=o.begin();i!=o.end();++i){
        const auto k=i.key().toLower();
        if((k.contains("target")||k.contains("object id")||k.contains("parent id"))&&i->isDouble()&&i->toDouble()>0)
            refs[quint32(i->toDouble())]<<path+"/"+i.key();
        else logicReferences(*i,path+"/"+i.key(),refs);
    }}
}
QJsonObject publicRow(QJsonObject r){r.remove("data");r.remove("after");return r;}
}

bool CAiEditAdapter::objectTool(const QString& s){return QStringList{"edit_objects","rebuild_ai_graph","query_passability","validate_map"}.contains(s);}
bool CAiEditAdapter::navigationStale(MainWindow* w){
    auto* v=w->view();if(!v)return false;
    if(v->m_aiGraphRegeneratedFresh&&!v->m_aiGraphPreviewUsesLoadedSource)return false;
    bool stale=v->m_canvasNavigationStale;
    for(int i=qMax(0,w->m_undoStack->cleanIndex());i<w->m_undoStack->index();++i){
        auto* command=w->m_undoStack->command(i);
        if(objectCommand(command)&&command->text().endsWith("rebuild_ai_graph"))stale=false;
        else if(aiGraphUndoCommandInvalidatesPreview(command))stale=true;
    }
    return stale;
}

QSharedPointer<const ei::SProfileDatabaseSnapshot> CAiEditAdapter::objectDatabase(MainWindow* w){
    const auto original=ei::ProfileDatabaseContext::instance().current();if(!original)return original;
    // The manual scenery gallery uses this database-side, author-owned catalog,
    // not the character RaceModels table. Extend only the detached MCP view.
    ei::TechnicalModelCatalog catalog;QString error;
    if(!catalog.load(ei::TechnicalModelCatalog::pathForDatabase(original->sourceArchivePath),&error))return original;
    const auto document=QJsonDocument::fromJson(catalog.stagedBytes()).object();
    const auto portable=document["modelTextures"].toObject();
    const auto profile=document["profiles"].toObject()[ei::TechnicalModelCatalog::profileKey(w->activeMapRoots().toList())].toObject()["modelTextures"].toObject();
    QSet<QString> creatures;for(const auto& race:original->raceModels)if(race.typeId>=50&&race.typeId<=52)creatures.insert(race.model.toLower());
    QSet<QString> available;for(const auto& texture:CTextureList::getInstance()->textureList())available.insert(texture.toLower());
    auto copy=QSharedPointer<ei::SProfileDatabaseSnapshot>::create(*original);
    for(const auto& name:CObjectList::getInstance()->figureList()){
        const auto key=ei::TechnicalModelCatalog::modelKey(name);
        if(key.isEmpty()||creatures.contains(key)||original->raceModels.contains(key))continue;
        const auto choices=(profile.contains(key)?profile[key]:portable[key]).toArray();
        ei::SRaceModelDatabaseRecord record;record.name=key;record.typeId=54;record.model=key;
        for(const auto& choice:choices){auto texture=choice.toString().toLower();if(texture.endsWith(".mmp"))texture.chop(4);if(available.contains(texture))record.primaryTextures.append(texture);}
        record.secondaryTextures=QStringList{"default0"};copy->raceModels["static:"+key]=record;
    }
    return copy;
}

ai::EditPlan CAiEditAdapter::captureObjects(MainWindow* w,const QString& method,const QJsonObject& args){
    ai::EditPlan p;p.method=method;p.args=args;p.description=QString::fromUtf8("ИИ: ")+method;
    const auto fail=[&](QString e){p.error=e;return p;};auto* v=w->view();
    if(busy(w))return fail("Editor gesture/load/modal interaction in progress; retry after it finishes");
    if(!v->m_pLand->isMprLoad())return fail("Open a terrain map first");
    p.revision=revision(w);
    p.args["_bounds"]=QJsonArray{v->m_terrainEdit.vertexCountX()-1,v->m_terrainEdit.vertexCountY()-1};
    const bool edit=method=="edit_objects"||method=="rebuild_ai_graph";
    if(edit&&args["revision"].toString()!=p.revision)return fail("Document revision conflict; call get_revision and prepare again");
    p.graphStale=navigationStale(w);
    if(method=="query_passability"||method=="rebuild_ai_graph"){
        if(!v->captureAiGraphPreviewBuildInput(&p.graphInput,&p.error))return p;
        if(p.graphInput.graphWidth*p.graphInput.graphHeight>16384)return fail("AI graph budget: at most 16384 graph cells (512x512 metres)");
    }
    if(method=="query_passability")return p;
    if(method=="edit_objects"){
        const auto action=args["action"].toString();
        if(action=="place"&&!args.contains("objects"))return fail("place requires objects");
        if(action!="place"&&args.contains("objects"))return fail("objects is only valid for action=place");
        if(action=="place"&&(args.contains("ids")||args.contains("area")||args.contains("delta")||args.contains("pivot")||args.contains("rotation")))return fail("place uses per-object coordinates/rotation; selectors and group transforms are invalid");
        if(action=="delete"&&(args.contains("delta")||args.contains("rotation")||args.contains("pivot")))return fail("delete does not accept transform parameters");
        if(action=="transform"&&!args.contains("delta")&&!args.contains("rotation"))return fail("transform requires delta or rotation");
        if(args.contains("area")){
            const auto area=args["area"].toObject();
            if(area["shape"]=="rectangle"&&(!area.contains("x")||!area.contains("y")||!area.contains("width")||!area.contains("height")))return fail("rectangle requires x,y,width,height");
            if(area["shape"]=="polygon"&&!area.contains("points"))return fail("polygon requires 3..128 points");
        }
    }
    p.family=w->mobFamilyValidationRequest(method!="validate_map");
    p.reservedObjectIds=w->m_sessionAllocatedNids;
    // Never silently validate old disk data when a live projection is unsafe.
    for(auto* mob:v->m_aMob){
        const auto path=mob->filePath().absoluteFilePath();
        const auto expected=QCryptographicHash::hash(mob->originalData(),QCryptographicHash::Sha256).toHex();
        if(edit&&hashFile(path)!=expected)return fail("Document revision conflict: loaded MOB changed on disk; reopen before editing: "+path);
        p.fileHashes[path]=expected;
        p.reservedObjectIds.unite(mob->occupiedMapIds());
        QByteArray bytes;CValidationReport report;
        if(!mob->buildValidatedCandidate(&bytes,&report)){
            if(method!="validate_map")return fail("Live MOB candidate is unsafe: "+mob->filePath().absoluteFilePath()+"\n"+report.toText());
            bytes.clear();p.captureFindings.append(finding("Error","mob.live_candidate_unsafe","Live MOB cannot be serialized safely",mob->filePath().absoluteFilePath()));
        }
        p.family.inMemoryMobData[QDir::cleanPath(mob->filePath().absoluteFilePath()).toLower()]=bytes;
        if(method=="validate_map")for(const auto& f:report.findings())p.captureFindings.append(finding(f.severity==EValidationSeverity::Info?"Info":f.severity==EValidationSeverity::Warning?"Warning":"Error",f.code,f.message,f.location));
        QHash<CNode*,QPair<QByteArray,QByteArray>> templates;
        if(method=="edit_objects"&&args["action"]=="copy"){
            QList<CNode*> selectedNodes;
            for(auto* n:mob->nodes())if(n->hasRawObjectProvenance()&&selected(row(mob,n),args))selectedNodes.append(n);
            if(selectedNodes.size()>ObjectLimit)return fail("Object operation limit500 exceeded; narrow the selector");
            if(!selectedNodes.isEmpty()&&!mob->rawObjectTemplatesBytes(selectedNodes,&templates))return fail("Cannot capture exact copy provenance");
        }
        for(auto* n:mob->nodes()){
            auto r=row(mob,n);auto data=r["data"].toObject();
            if(templates.contains(n)){
                const auto source=templates[n].first,clean=templates[n].second;
                data["_Endurance raw provenance"]=QJsonObject{{"sourceDocumentSha256",QString::fromLatin1(n->rawSourceDocumentSha256().toHex())},
                    {"sourceObjectPath",n->rawSourceObjectPath()},{"sourceSubtreeSha256",QString::fromLatin1(n->rawSourceSubtreeSha256().toHex())},
                    {"sourceSubtreeBase64",QString::fromLatin1(source.toBase64())},{"cleanSubtreeBase64",QString::fromLatin1(clean.toBase64())}};
                r["data"]=data;
            }
            p.objects.append(r);
        }
    }
    if(edit){
        if(v->m_aMob.isEmpty())return fail("Load the primary MOB first");
        for(auto* mob:v->m_aMob){QString e;auto lease=CProcessEditLock::acquire(mob->filePath().absoluteFilePath(),&e);if(!lease)return fail(e);
            ai::EditMap map;map.path=mob->filePath().absoluteFilePath();map.editLease=lease;p.maps.append(map);}
    }
    // Destination is always the primary MOB of this map, never an AddMob/unit.
    const auto primary=QFileInfo(v->m_pLand->filePath().absolutePath(),v->m_pLand->filePath().completeBaseName()+".mob").absoluteFilePath();
    for(auto* mob:v->m_aMob)if(CProcessEditLock::identity(mob->filePath().absoluteFilePath())==CProcessEditLock::identity(primary))p.mobPath=mob->filePath().absoluteFilePath();
    if(method=="rebuild_ai_graph"&&p.mobPath.isEmpty())return fail("An opened primary MOB is required for an embedded AI graph");
    if(method=="edit_objects"&&args["action"]=="place"){
        if(p.mobPath.isEmpty())return fail("Load the primary MOB before placing objects");
        const auto db=objectDatabase(w);if(!db)return fail("No profile database loaded");
        const auto catalogPath=ei::TechnicalModelCatalog::pathForDatabase(db->sourceArchivePath);
        if(QFileInfo::exists(catalogPath))p.fileHashes[catalogPath]=hashFile(catalogPath);
        for(const auto& value:args["objects"].toArray()){
            const auto spec=value.toObject();const auto key=spec["prototype"].toString().toLower();
            auto it=db->raceModels.constFind(key);
            if(it==db->raceModels.constEnd()){
                for(auto j=db->raceModels.constBegin();j!=db->raceModels.constEnd();++j)if(j->name.compare(key,Qt::CaseInsensitive)==0){it=j;break;}
            }
            if(it==db->raceModels.constEnd())return fail(db->monsters.contains(key)?"Units are read-only in272; choose a static prototype":"Unknown static database prototype; use search_database(category=prototypes)");
            if(it->typeId>=50&&it->typeId<=52)return fail("Units are read-only in272; unit placement is refused");
            if(it->typeId!=54)return fail("Prototype is not a static world object (database type54); units and other record types are refused in272");
            if(it->primaryTextures.isEmpty())return fail("Static prototype has no installed authored texture binding; choose/bind it in the manual model catalog first");
            CWorldObj defaults;auto data=defaults.toJson();
            data["Model name"]=it->model;data["Object template"]=it->name;
            data["Primary texture"]=it->primaryTextures.value(0);data["Secondary texture"]=it->secondaryTextures.value(0,"default0");
            if(spec.contains("primaryTexture")){
                const auto name=spec["primaryTexture"].toString().toLower();
                if(!it->primaryTextures.contains(name,Qt::CaseInsensitive))return fail("Primary texture must be one of search_database static prototype textures");
                data["Primary texture"]=name;
            }
            if(spec.contains("secondaryTexture")){
                const auto name=spec["secondaryTexture"].toString().toLower();
                if(name!="default0"&&!QStringList(CTextureList::getInstance()->textureList()).contains(name,Qt::CaseInsensitive))return fail("Secondary texture is unavailable in the active profile");
                data["Secondary texture"]=name;
            }
            if(spec.contains("parts")){
                const auto allowed=CObjectList::getInstance()->figurePartNames(it->model);
                for(const auto& part:spec["parts"].toArray())if(!allowed.contains(part.toString(),Qt::CaseInsensitive))return fail("Unknown model part: "+part.toString());
                data["Object parts"]=spec["parts"];
            }
            auto b=base(data);b["Map name"]=spec["name"].toString(it->name);b["Position"]=QJsonArray{spec["x"],spec["y"],spec["z"].toDouble(0)};
            const auto rot=spec["rotation"].toArray();const auto q=QQuaternion::fromEulerAngles(vector(rot.isEmpty()?QJsonArray{0,0,0}:rot));
            b["Rotation"]=QJsonArray{q.x(),q.y(),q.z(),q.scalar()};
            if(spec.contains("scale"))b["Complection"]=spec["scale"];
            data=setBase(data,b);if(spec.contains("shadow"))data["Is shadow?"]=spec["shadow"];
            CWorldObj seed(data);QJsonObject prepared;CValidationReport report;
            if(!CMob::prepareFreshTechnicalNode(&seed,&prepared,&report))return fail(report.toText());
            p.changes.append(QJsonObject{{"mob",p.mobPath},{"data",prepared},{"kind","create"}});
        }
    }
    // The effective profile map.txt contains transition exit IDs. Only routes
    // belonging to this MOB family reserve/reference IDs in this namespace.
    for(const auto& entry:w->m_mapCatalog.topologyEntries){
        const auto name=QFileInfo(entry.mobName).completeBaseName();bool family=false;
        for(auto* mob:v->m_aMob)family|=mob->filePath().completeBaseName().compare(name,Qt::CaseInsensitive)==0;
        if(!family)continue;
        for(const auto& route:entry.travelZones)if(route.exitId>0)p.captureFindings.append(finding("Info","map_txt.reference","map.txt transition exit",entry.sourceArchive+"/map.txt/"+entry.zoneId,double(route.exitId)));
    }
    if(!w->m_mapCatalog.sourceArchive.isEmpty())p.fileHashes[w->m_mapCatalog.sourceArchive]=hashFile(w->m_mapCatalog.sourceArchive);
    return p;
}

static ai::EditPlan prepareObjectValues(ai::EditPlan p,const ai::Cancellation& cancel){
    const auto fail=[&](QString e){p.error=e;return p;};
    if(cancel->load())return fail("Cancelled; live documents unchanged");
    if(p.method=="query_passability"||p.method=="rebuild_ai_graph"){
        p.graphResult=p.method=="rebuild_ai_graph"?CAiGraphPreviewBuilder::build(p.graphInput)
            :CAiGraphPreviewBuilder::passability(p.graphInput,p.args["layer"].toInt());
        if(cancel->load())return fail("Cancelled; live documents unchanged");
        if(!p.graphResult.valid)return fail(p.graphResult.error);
        if(p.method=="rebuild_ai_graph"){
            auto& doc=p.graphResult.document;
            if(!doc.recolorConnectedComponents(&p.error)||!doc.validateRuntimeCompatibility(p.graphInput.graphWidth,p.graphInput.graphHeight,&p.error))return p;
            p.mobBytes=doc.serialize(&p.error);
            p.summary={{"revision",p.revision},{"graphWidth",doc.width()},{"graphHeight",doc.height()},
                {"layers",8},{"graphSha256",QString::fromLatin1(QCryptographicHash::hash(p.mobBytes,QCryptographicHash::Sha256).toHex())},
                {"collisionProxies",p.graphResult.collisionProxyCount},{"authoredProxies",p.graphResult.authoredCollisionProxyCount},{"blockedAiTiles",p.graphResult.blockedAiTiles},
                {"builder","Navigation AI / Update graph"},{"builderWarnings",QJsonArray::fromStringList(p.graphResult.warnings)}};return p;
        }
        const int layer=p.args["layer"].toInt(),width=p.graphInput.graphWidth*8,height=p.graphInput.graphHeight*8;
        const auto& mask=p.graphResult.layerPassability.value(layer);QJsonArray results;
        if(mask.size()!=width*height)return fail("Requested layer passability is unavailable");
        const double radius=p.args["radius"].toDouble(8);const int r=int(std::ceil(radius*2));
        for(const auto& v:p.args["points"].toArray()){
            if(cancel->load())return fail("Cancelled");auto xy=v.toArray();const double x=xy[0].toDouble(),y=xy[1].toDouble();
            const int ix=ei::runtimeAiTileCoordinate(float(x)),iy=ei::runtimeAiTileCoordinate(float(y));
            const bool in=ix>=0&&iy>=0&&ix<width&&iy<height,pass=in&&mask[iy*width+ix]!=0;
            QJsonValue nearest;double best=radius*radius+1e-10;int bestIndex=-1;
            if(pass){nearest=QJsonObject{{"position",QJsonArray{x,y,p.graphResult.navigationSurfaceHeights.value(iy*width+ix)}},{"distance",0}};}
            else for(int cy=qMax(0,iy-r);cy<=qMin(height-1,iy+r);++cy)for(int cx=qMax(0,ix-r);cx<=qMin(width-1,ix+r);++cx){
                if(!mask[cy*width+cx])continue;double dx=(cx+0.5)*0.5-x,dy=(cy+0.5)*0.5-y,d=dx*dx+dy*dy;
                if(d<best){best=d;bestIndex=cy*width+cx;}
            }
            if(bestIndex>=0)nearest=QJsonObject{{"position",QJsonArray{(bestIndex%width+0.5)*0.5,(bestIndex/width+0.5)*0.5,p.graphResult.navigationSurfaceHeights.value(bestIndex)}},{"distance",std::sqrt(best)}};
            results.append(QJsonObject{{"point",xy},{"inBounds",in},{"passable",pass},{"nearest",nearest}});
        }
        p.summary={{"capturedRevision",p.revision},{"layer",layer},{"layerLabel",CAiGraphDocument::layerLabel(layer)},
            {"graphStale",p.graphStale},{"warning",p.graphStale?"Stored AI graph is stale; query uses current terrain/static collision classifier. Rebuild graph before Save.":""},
            {"source","Native legacy 0.5m layer classifier, current document snapshot"},{"builderWarnings",QJsonArray::fromStringList(p.graphResult.warnings)},{"points",results}};return p;
    }
    const auto family=CMobFamilyValidator::validate(p.family);
    QMap<quint32,QStringList> refs;QSet<quint32> occupied;
    QJsonArray findings=p.captureFindings;
    for(const auto& f:family.findings){
        const auto level=f.severity==EMobFamilyFindingSeverity::Info?"Info":f.severity==EMobFamilyFindingSeverity::Warning?"Warning":"Error";
        auto out=finding(level,f.code,f.message,f.locations.join(";"),f.hasNid?QJsonValue(double(f.nid)):QJsonValue());
        if(f.hasNid)for(const auto& r:p.objects)if(r.toObject()["id"].toDouble()==f.nid){out["coordinates"]=r.toObject()["position"];break;}
        findings.append(out);
    }
    for(const auto& member:family.members){
        if(member.missing||!member.error.isEmpty()){
            if(p.method=="validate_map")continue;
            return fail("Incomplete MOB family: "+member.path+": "+member.error);
        }
        // Protect disk dependencies until commit; loaded candidates may differ
        // from disk and are protected separately by the live revision token.
        const auto diskHash=hashFile(member.path);
        if(p.method!="validate_map"&&p.fileHashes.contains(member.path)&&p.fileHashes[member.path]!=diskHash)return fail("Document revision conflict: MOB dependency changed during preparation");
        if(!p.fileHashes.contains(member.path))p.fileHashes[member.path]=diskHash;
        for(const auto& ref:CScriptReferenceAnalyzer::findObjectIds(member.script))refs[ref.objectId]<<member.path+":"+QString::number(ref.line)+":"+QString::number(ref.column)+" "+ref.context;
        for(const auto& n:member.objects){if(n.hasNid)occupied.insert(n.nid);if(n.hasParentId&&n.parentId>0&&n.parentId!=quint32(-1))refs[n.parentId]<<member.path+n.sectionPath+"/ParentID";}
        for(const auto& n:member.auxiliaryIds)occupied.insert(n.id);
        for(const auto& n:member.objects){
            if(n.useInScript&&n.hasNid)refs[n.nid]<<member.path+n.sectionPath+"/InScript";
            if(n.name.isEmpty()||!n.hasNid)continue;
            const QRegularExpression lookup("\\bGetObjectByName\\s*\\(\\s*[\"']"+QRegularExpression::escape(n.name)+"[\"']\\s*\\)",QRegularExpression::CaseInsensitiveOption);
            for(const auto& scriptMember:family.members){auto matches=lookup.globalMatch(scriptMember.script);while(matches.hasNext()){
                const auto m=matches.next();refs[n.nid]<<scriptMember.path+":"+QString::number(scriptMember.script.left(m.capturedStart()).count('\n')+1)+" GetObjectByName";
            }}
        }
    }
    for(const auto& r:p.objects){auto o=r.toObject();logicReferences(o["data"],o["mob"].toString()+"/"+QString::number(o["id"].toDouble(),'f',0),refs);}
    for(const auto& f:p.captureFindings){auto o=f.toObject();if(o["code"]=="map_txt.reference")refs[quint32(o["id"].toDouble())]<<o["path"].toString();}
    if(p.method=="validate_map"){
        if(p.graphStale)findings.append(finding("Warning","ai_graph.stale","AI graph is stale after document edits; use rebuild_ai_graph",p.mobPath));
        for(auto i=refs.constBegin();i!=refs.constEnd();++i)if(i.key()!=0&&i.key()!=quint32(-1)&&!occupied.contains(i.key()))
            findings.append(finding("Warning","reference.missing","Reference to absent object ID "+QString::number(i.key()),i.value().join(";"),double(i.key())));
        QJsonArray info,issues;int warnings=0,errors=0;
        for(const auto& f:findings){auto o=f.toObject();if(o["level"]=="Info")info.append(o);else{issues.append(o);warnings+=o["level"]=="Warning";errors+=o["level"]=="Error";}}
        if(issues.size()+info.size()>2000)return fail("Validation finding limit 2000 exceeded; no findings silently truncated");
        p.summary={{"capturedRevision",p.revision},{"findings",issues},{"info",info},{"warnings",warnings},{"errors",errors},{"graphStale",p.graphStale},{"familyMembers",family.members.size()}};
        if(QJsonDocument(p.summary).toJson(QJsonDocument::Compact).size()>ai::MaxResult)return fail("Validation response exceeds 512KiB; inspect family in editor");return p;
    }
    if(family.criticalCount()||family.incompleteCount())return fail("MOB family contains errors; use validate_map before editing");
    const auto a=p.args;const auto action=a["action"].toString();
    if(action!="place"){
        if(!a.contains("ids")&&!a.contains("area"))return fail("Specify ids or a rectangle/polygon area; whole-map edits are refused");
        QSet<quint32> seen;
        for(const auto& value:p.objects){const auto r=value.toObject();if(!selected(r,a))continue;
            if(r["type"].toInt()==eUnit)return fail("Units are read-only in272; selected unit ID "+QString::number(r["id"].toDouble(),'f',0));
            if(!ei::usesTerrainRelativeMapObjectHeight(ENodeType(r["type"].toInt())))return fail("Selected record is not a supported static world object");
            const auto id=quint32(r["id"].toDouble());if(seen.contains(id))return fail("Ambiguous duplicate ID in MOB family");seen.insert(id);
            if(p.changes.size()>=ObjectLimit)return fail("Object operation limit500 exceeded; narrow the selector");
            if(action=="delete"||action=="transform"){
                const auto references=refs.value(id);if(!references.isEmpty())return fail("Referenced object ID "+QString::number(id)+"; operation refused (no force):\n"+references.join("\n"));
            }
            auto change=r;change["kind"]=action=="delete"?"delete":action=="copy"?"create":"transform";
            if(action=="copy")change["mob"]=p.mobPath;
            p.changes.append(change);
        }
        for(const auto& id:a["ids"].toArray())if(!seen.contains(quint32(id.toDouble())))return fail("Requested ID is absent or excluded by filters: "+QString::number(id.toDouble(),'f',0));
        if(p.changes.isEmpty())return fail("No objects matched the selector");
    } else if(p.changes.isEmpty())return fail("place requires 1..500 objects");
    if(action=="copy"&&p.mobPath.isEmpty())return fail("Load primary MOB before copying objects");
    for(auto i=refs.constBegin();i!=refs.constEnd();++i)occupied.insert(i.key());
    occupied.unite(p.reservedObjectIds);
    QJsonArray changes,affected;
    QVector3D pivot=vector(a["pivot"].toArray().isEmpty()?QJsonArray{0,0,0}:a["pivot"].toArray());
    if(!a.contains("pivot")&&action!="place"){for(const auto& c:p.changes)pivot+=vector(base(c.toObject()["data"].toObject())["Position"].toArray());pivot/=p.changes.size();}
    const auto rotation=QQuaternion::fromAxisAndAngle(0,0,1,float(a["rotation"].toDouble(0)));
    const auto delta=vector(a["delta"].toArray().isEmpty()?QJsonArray{0,0,0}:a["delta"].toArray());
    for(const auto& c:p.changes){if(cancel->load())return fail("Cancelled");auto change=c.toObject();auto data=change["data"].toObject();auto b=base(data);
        if(change["kind"]=="create"){
            quint32 id=0;
            if(!family.suggestReplacementNid(p.mobPath,&id,nullptr,occupied)||!CMobFamilyValidator::isAuthorableObjectNid(id))return fail("No free object ID in primary MOB authoring ranges; no IDs were changed");
            occupied.insert(id);if(action=="copy")change["sourceId"]=change["id"];
            change["id"]=double(id);b["Id"]=double(id);
        }
        if(action=="transform"||action=="copy"){
            const auto pos=vector(b["Position"].toArray());b["Position"]=vector(pivot+rotation.rotatedVector(pos-pivot)+delta);
            const auto q=b["Rotation"].toArray();const QQuaternion old(float(q[3].toDouble()),float(q[0].toDouble()),float(q[1].toDouble()),float(q[2].toDouble()));
            const auto after=rotation*old;b["Rotation"]=QJsonArray{after.x(),after.y(),after.z(),after.scalar()};
        }
        data=setBase(data,b);change["after"]=data;
        const auto pos=vector(b["Position"].toArray());
        if(change["kind"]!="delete"&&(pos.x()<0||pos.y()<0||pos.x()>p.graphInput.graphWidth*4||pos.y()>p.graphInput.graphHeight*4)){
            // Dimensions are captured without requiring a graph/resource build.
            const auto bounds=p.args["_bounds"].toArray();
            if(pos.x()<0||pos.y()<0||pos.x()>bounds[0].toDouble()||pos.y()>bounds[1].toDouble())return fail("Resulting object is outside active terrain bounds");
        }
        auto out=publicRow(change);out["positionAfter"]=b["Position"];out["rotationAfter"]=b["Rotation"];affected.append(out);changes.append(change);
    }
    p.changes=changes;p.summary={{"revision",p.revision},{"action",action},{"count",changes.size()},{"affected",affected},{"limit",ObjectLimit}};return p;
}
ai::EditPlan CAiEditAdapter::prepareObjects(ai::EditPlan p,const ai::Cancellation& cancel){
    try{return prepareObjectValues(p,cancel);}
    catch(const std::exception& e){p.error=QString::fromUtf8(e.what());return p;}
    catch(...){p.error="Object/graph preparation failed; live documents unchanged";return p;}
}

class CAiObjectCommand : public QUndoCommand,public IAiGraphPreviewInvalidatingCommand {
public:
    struct Change{CMob* mob;CNode* node;int index;QString kind;QVector3D before,after;QQuaternion beforeRot,afterRot;};
    QPointer<MainWindow> window;QVector<Change> changes;CMob* graphMob{nullptr};QByteArray beforeGraph,afterGraph;
    CAiGraphDocument beforeDocument,afterDocument;SAiGraphPreviewBuildResult beforeResult,afterResult;
    bool beforeFresh{false},beforeLoaded{true},beforeCanvasStale{false},first{true};
    QString beforeSource,beforeDisplaySource;
    bool invalidatesAiGraphPreview()const override{return !graphMob;}
    void run(bool forward){auto* v=window?window->view():nullptr;if(!v)return;
        v->makeCurrent();v->beginObjectEditBatch();
        const auto apply=[&](Change& c){
            if(c.kind=="create"){if(forward)c.mob->restoreDeletedNode(c.node,c.index);else c.mob->deleteNode(c.node);}
            else if(c.kind=="delete"){if(forward)c.mob->deleteNode(c.node);else c.mob->restoreDeletedNode(c.node,c.index);}
            else{auto pos=forward?c.after:c.before;c.node->setRot(forward?c.afterRot:c.beforeRot);c.node->updatePos(pos);}
            v->setDurty(c.mob);
        };
        if(forward)for(auto& c:changes)apply(c);else for(int i=changes.size()-1;i>=0;--i)apply(changes[i]);
        if(graphMob){
            graphMob->setAiGraphData(forward?afterGraph:beforeGraph);
            v->cancelPendingAiGraphPreviewRegeneration();
            v->m_aiGraphRegeneratedDocument=forward?afterDocument:beforeDocument;v->m_aiGraphRegeneratedResult=forward?afterResult:beforeResult;
            v->m_aiGraphRegeneratedFresh=forward?true:beforeFresh;v->m_aiGraphPreviewUsesLoadedSource=forward?false:beforeLoaded;
            v->m_canvasNavigationStale=forward?false:beforeCanvasStale;
            v->m_aiGraphRegeneratedSource=forward?QString::fromUtf8("ИИ: обновлённый встроенный граф"):beforeSource;
            v->m_aiGraphSource=forward?v->m_aiGraphRegeneratedSource:beforeDisplaySource;
            v->m_aiGraphDocument=forward?afterDocument:(beforeLoaded?v->m_aiGraphLoadedDocument:beforeDocument);
            v->m_aiGraphOverlayDirty=true;v->setDurty(graphMob);v->syncAiGraphPreviewWidget();
        } else v->m_canvasNavigationStale=forward?true:beforeCanvasStale;
        v->collectObjectTreeData();v->viewParameters();v->endObjectEditBatch();v->update();
        window->updateDocumentActionAvailability();window->scheduleMobFamilyValidation();
    }
    void undo()override{run(false);}
    void redo()override{if(first){first=false;return;}run(true);}
};
bool CAiEditAdapter::objectCommand(const QUndoCommand* c){return dynamic_cast<const CAiObjectCommand*>(c)!=nullptr;}

QJsonObject CAiEditAdapter::applyObjects(MainWindow* w,const ai::EditPlan& p){
    for(auto i=p.fileHashes.begin();i!=p.fileHashes.end();++i)if(i.value().isEmpty()||hashFile(i.key())!=i.value())return ai::error("Document revision conflict: family/map.txt dependency changed; prepare again");
    auto* v=w->view();QScopedPointer<CAiObjectCommand> command(new CAiObjectCommand);command->window=w;command->setText(p.description);
    command->beforeCanvasStale=v->m_canvasNavigationStale;
    const auto mobFor=[&](QString path)->CMob*{for(auto* mob:v->m_aMob)if(mob->filePath().absoluteFilePath()==path)return mob;return nullptr;};
    if(p.method=="rebuild_ai_graph"){
        auto* mob=mobFor(p.mobPath);if(!mob)return ai::error("Primary MOB was unloaded");
        command->graphMob=mob;command->beforeGraph=mob->aiGraphData();command->afterGraph=p.mobBytes;
        command->beforeDocument=v->m_aiGraphRegeneratedDocument;command->afterDocument=p.graphResult.document;
        command->beforeResult=v->m_aiGraphRegeneratedResult;command->afterResult=p.graphResult;
        command->beforeFresh=v->m_aiGraphRegeneratedFresh;command->beforeLoaded=v->m_aiGraphPreviewUsesLoadedSource;
        command->beforeCanvasStale=v->m_canvasNavigationStale;command->beforeSource=v->m_aiGraphRegeneratedSource;
        command->beforeDisplaySource=v->m_aiGraphSource;
    }else{
        v->makeCurrent();
        for(const auto& value:p.changes){const auto c=value.toObject();auto* mob=mobFor(c["mob"].toString());if(!mob)return ai::error("MOB was unloaded");
            const auto kind=c["kind"].toString();CNode* node=nullptr;
            if(kind=="create"){
                node=mob->createNode(c["after"].toObject());if(!node){for(const auto& old:command->changes)if(old.kind=="create")old.mob->discardUncommittedNode(old.node);return ai::error("Native object creation failed; unchanged");}
                node->setMapId(quint32(c["id"].toDouble()));node->setState(eDraw);auto pos=node->position();node->updatePos(pos);
            }else node=mob->nodeByMapId(quint32(c["id"].toDouble()));
            if(!node)return ai::error("Selected object disappeared");
            const auto b=base(c["after"].toObject());const auto q=b["Rotation"].toArray();
            command->changes.append({mob,node,mob->nodes().indexOf(node),kind,node->position(),vector(b["Position"].toArray()),node->rotation(),QQuaternion(float(q[3].toDouble()),float(q[0].toDouble()),float(q[1].toDouble()),float(q[2].toDouble()))});
        }
        // Created nodes already exist; detach once so the forward operation has
        // the same provenance-preserving restore path as later Redo.
        for(auto& c:command->changes)if(c.kind=="create")c.mob->deleteNode(c.node);
    }
    command->run(true);
    for(auto* mob:v->m_aMob){QByteArray bytes;CValidationReport report;
        if(!mob->buildValidatedCandidate(&bytes,&report)){command->run(false);return ai::error("Native candidate rejected; complete operation rolled back: "+report.toText());}
    }
    for(const auto& c:command->changes)if(c.kind=="create")w->m_sessionAllocatedNids.insert(c.node->mapId());
    w->m_undoStack->push(command.take());
    auto result=p.summary;result["status"]="applied";result["revision"]=revision(w);result["undo"]=p.description;return result;
}
