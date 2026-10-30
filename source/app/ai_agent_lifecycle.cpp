#include "ai_agent_edit.h"
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "database_document.h"
#include "resourcemanager.h"
#include "resource_profile.h"
#include "map_save_batch.h"
#include <QtWidgets>

namespace {
QString key(const QString& path) { return CWorldTerrainSession::key(path); }
QByteArray digest(const QString& path) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly))return {};
    QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&f))return {};return h.result();
}
bool authorized(const QString& path,const QStringList& roots) {
    const auto real=QFileInfo(path).canonicalFilePath();if(real.isEmpty())return false;
    for(const auto& root:roots){const auto base=QFileInfo(root).canonicalFilePath();
        if(!base.isEmpty()&&real.startsWith(base+"/",Qt::CaseInsensitive))return true;}
    return false;
}
QString resolve(const ai::EditPlan& p,QString wanted,const QString& extension) {
    if(wanted.isEmpty()||QDir::isAbsolutePath(wanted)||wanted.contains("..")||wanted.contains(':')||wanted.contains('\\'))return {};
    if(p.mapAliases.contains(wanted.toCaseFolded())) {
        const auto alias=p.mapAliases.value(wanted.toCaseFolded());
        if(QFileInfo(alias).suffix().compare(extension,Qt::CaseInsensitive)==0&&authorized(alias,p.mapRoots))return QFileInfo(alias).canonicalFilePath();
    }
    const auto slash=wanted.indexOf('/');
    if(slash>0){bool ok=false;const int root=wanted.left(slash).toInt(&ok);
        if(ok&&root>=0&&root<p.mapRoots.size()) {const auto path=QDir(p.mapRoots[root]).filePath(wanted.mid(slash+1));
            if(QFileInfo(path).suffix().compare(extension,Qt::CaseInsensitive)==0&&authorized(path,p.mapRoots))return QFileInfo(path).canonicalFilePath();}
        return {};
    }
    if(!wanted.endsWith("."+extension,Qt::CaseInsensitive))wanted+="."+extension;
    for(const auto& root:p.mapRoots){const auto path=QDir(root).filePath(wanted);
        if(authorized(path,p.mapRoots))return QFileInfo(path).canonicalFilePath();}
    return {};
}
bool readMpr(const QString& path,CMprDocument* doc,QString* error) {
    CValidationReport report;
    if(path.isEmpty()||QFileInfo(path).size()>ai::MaxMapBytes||!doc->load(QFileInfo(path),&report)||report.hasErrors()) {
        *error="Profile MPR missing, invalid or larger than 64 MiB: "+path+" "+report.toText();return false;}
    return true;
}
}

bool CAiEditAdapter::lifecycle(const QString& m) {
    return QStringList{"open_map","close_map","place_map_in_world","set_world_position","create_mob"}.contains(m);
}
bool CAiEditAdapter::dirty(MainWindow* w) {
    auto* v=w->view();
    if(v->m_pLand->isDirty()||v->m_terrainEdit.isDirty()||v->hasDirtyWorldTerrain()||w->hasTravelChanges())return true;
    for(auto* mob:v->m_aMob){bool draft=false;v->scenarioScriptText(mob,&draft);if(mob->isDirty()||draft)return true;}
    QString error;return w->m_databaseDocument&&w->m_databaseDocument->hasUnappliedChanges(&error);
}
ai::EditPlan CAiEditAdapter::captureLifecycle(MainWindow* w,const QString& method,const QJsonObject& args) {
    ai::EditPlan p;p.method=method;p.args=args;p.revision=revision(w);
    if(busy(w)) {p.error="Editor interaction/load in progress; retry";return p;}
    if(args["revision"].toString()!=p.revision){p.error="Document revision conflict; get_revision first";return p;}
    if((method=="open_map"||method=="close_map"||method=="create_map")&&dirty(w)) {
        p.error="Unsaved changes: save first, or ask the human to resolve them; workspace unchanged";return p;}
    p.mapRoots=w->activeMapRoots().toList();
    for(const auto& e:w->m_mapCatalog.entries){p.mapAliases.insert(e.zoneId.toCaseFolded(),e.mprPath);}
    auto* v=w->view();if(v->m_pLand->isMprLoad())p.activePath=v->m_pLand->filePath().canonicalFilePath();
    if(method=="create_map") {
        const auto name=args["name"].toString();
        if(!QRegularExpression("^[A-Za-z][A-Za-z0-9_-]{0,39}$").match(name).hasMatch()
           ||QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$",QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())p.error="Use an ASCII map name (1..40 letters/digits/_/-), starting with a letter; no paths or device names";
        const int root=args["root"].toInt();
        if(root<0||root>=p.mapRoots.size()||!QFileInfo(p.mapRoots.value(root)).isDir()||QFileInfo(p.mapRoots.value(root)).isSymLink())p.error="Active profile map root unavailable";
        if(!p.error.isEmpty())return p;
        p.destination=QDir(QFileInfo(p.mapRoots[root]).canonicalFilePath()).filePath(name+".mpr");
        if(QFileInfo::exists(p.destination)||QFileInfo(p.destination).isSymLink())p.error="Map already exists; create_map never overwrites";
        p.templatePath=args.contains("template")?QString():p.activePath;
        p.sources.families=CTerrainAtlasPresetCatalog::globalUniformFamilies();
        p.sources.archives=CTextureList::getInstance()->configuredResourceArchivePaths();
        p.sources.mapRoots=p.mapRoots;p.sources.reference=CTextureList::getInstance()->terrainAuthoringArchivePath();
        p.sources.referenceHash=CTextureList::getInstance()->terrainAuthoringReferenceSha256();
        const auto nameMaterial=args["materialName"].toString();int material=args["material"].toInt(),matches=0;
        for(const auto& f:p.sources.families)if(!nameMaterial.isEmpty()&&f.name.compare(nameMaterial,Qt::CaseInsensitive)==0){if(material&&material!=f.id)p.error="Material ID and name disagree";material=f.id;++matches;}
        if(!nameMaterial.isEmpty()&&matches!=1)p.error="Unknown or ambiguous profile materialName";
        if(material){bool known=false;for(const auto& f:p.sources.families)known|=f.id==material;if(!known)p.error="Unknown profile material";}
        p.args["material"]=material;
    }
    if(method=="place_map_in_world"||method=="set_world_position"||method=="open_map"||method=="create_mob") {
        w->refreshEditorWorld();p.worldDocument=w->m_worldMapLayoutDocument;p.profileId=w->activeResourceProfile().profileId;
        if(!w->m_lastWorldMapLayoutWarning.isEmpty())p.error=w->m_lastWorldMapLayoutWarning;
    }
    return p;
}

ai::EditPlan CAiEditAdapter::prepareLifecycle(ai::EditPlan p,const ai::Cancellation& cancel) {
    const auto fail=[&](const QString& e){p.error=e;return p;};QString error;const QJsonObject args=p.args;
    if(cancel->load())return fail("Cancelled; unchanged");
    if(p.method=="create_map") {
        if(args.contains("template"))p.templatePath=resolve(p,args["template"].toString(),"mpr");
        else if(p.templatePath.isEmpty()) {
            // Deterministic profile template, using the exact New MPR213 builder.
            // Never search arbitrary disks or require an already open document.
            for(const auto& root:p.mapRoots){for(const auto& file:QDir(root).entryInfoList({"*.mpr"},QDir::Files,QDir::Name))
                if(authorized(file.filePath(),p.mapRoots)){p.templatePath=file.canonicalFilePath();break;}
                if(!p.templatePath.isEmpty())break;}
        }
        if(!authorized(p.templatePath,p.mapRoots))return fail("Template must be inside active profile map roots; use list_profile_maps");
        CMprDocument doc;if(!readMpr(p.templatePath,&doc,&error))return fail(error+"; supply template from list_profile_maps");
        p.fileHashes[p.templatePath]=digest(p.templatePath);
        ai::EditMap m;m.path=p.templatePath;m.id=key(m.path);p.maps.append(m);
        return p;
    }
    if(p.method=="close_map")return p;
    const QString path=args.contains("map")?resolve(p,args["map"].toString(),"mpr"):p.activePath;
    if(!authorized(path,p.mapRoots))return fail("Map must be inside active profile map roots; use list_profile_maps");
    CMprDocument doc;if(!readMpr(path,&doc,&error))return fail(error);
    p.destination=path;p.fileHashes[path]=digest(path);
    if(p.method=="open_map") {
        QString leaseError;auto mapLease=CProcessEditLock::acquire(p.destination,&leaseError);
        auto mobLease=p.mobPath.isEmpty()?mapLease:CProcessEditLock::acquire(p.mobPath,&leaseError);
        if(!mapLease||!mobLease)return fail(leaseError);
        p.mobPath=CWorldMapLayoutDocument::companionMobPath(path);
        const auto layout=p.worldDocument.loadEditorWorld(p.profileId,p.mapRoots);
        if(layout.storageBlocked)return fail(layout.warning);
        for(const auto& zone:layout.zones)if(key(p.worldDocument.absoluteResourcePath(zone.mprPath))==key(path)&&!zone.mobPath.isEmpty())p.mobPath=p.worldDocument.absoluteResourcePath(zone.mobPath);
        if(QFileInfo::exists(p.mobPath)){
            if(!authorized(p.mobPath,p.mapRoots))return fail("MOB resolves outside profile roots");
            CMobDocument mob;CValidationReport report;
            if(QFileInfo(p.mobPath).size()>ai::MaxMapBytes||!mob.load(QFileInfo(p.mobPath),&report)||report.hasErrors()
                ||!CMob::validateEditableProjection(mob.originalBytes(),QFileInfo(p.mobPath),&error))return fail("Invalid primary MOB: "+error+report.toText());
            p.fileHashes[p.mobPath]=digest(p.mobPath);
        }else p.mobPath.clear();
        return p;
    }
    if(p.method=="create_mob") {
        if(path!=p.activePath)return fail("create_mob requires the active map; open_map first");
        p.destination=QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName()+".mob");
        if(QFileInfo::exists(p.destination)||QFileInfo(p.destination).isSymLink())return fail("MOB already exists; never overwrite");
        SMobParameterState source;
        if(args.contains("template")){
            const auto sourcePath=resolve(p,args["template"].toString(),"mob");
            if(sourcePath.isEmpty()||QFileInfo(sourcePath).size()>ai::MaxMapBytes||!CMob::readTemplateParameters(QFileInfo(sourcePath),&source,&error))return fail("Invalid profile MOB template: "+error);
            p.fileHashes[sourcePath]=digest(sourcePath);
        } else if(args["copyRanges"].toBool()||args["copyScript"].toBool()||args["copyDiplomacy"].toBool()||args["copyWorld"].toBool())return fail("Copy flags require a profile MOB template");
        const auto minimum=quint32(args["rangeMinimum"].toDouble(1)),maximum=quint32(args["rangeMaximum"].toDouble(999999));
        if(minimum>maximum)return fail("rangeMinimum exceeds rangeMaximum");
        auto ranges=args["copyRanges"].toBool()?(source.questMob?source.secondaryRanges:source.mainRanges):QVector<SRange>{SRange(minimum,maximum)};
        if(ranges.isEmpty())return fail("Template has no active ID range");
        if(args["copyWorld"].toBool()&&source.questMob)return fail("Supplemental template has no primary world settings");
        CMob generated;generated.setQuestMob(false);generated.ensureObjectSection();generated.setRanges(true,ranges);
        if(args["copyScript"].toBool())generated.setScript(source.script);
        if(args["copyDiplomacy"].toBool()){generated.setDiplomacyField(source.diplomacy);generated.setDiplomacyNames(source.diplomacyNames);}
        else if(args["defaultDiplomacy"].toBool())generated.generateDiplomacyTable();
        CWorldSet world;if(args["copyWorld"].toBool())world=source.worldSet;else world.setData(eWsTypeTime,QString::number(args["time"].toDouble(12)));
        generated.setWorldSet(world);generated.serializeMob(p.mobBytes,false);
        CMobDocument verified;CValidationReport report;
        if(!verified.loadBytes(p.mobBytes,p.destination,&report)||report.hasErrors()||verified.isQuestMob()||!verified.objects().isEmpty()
            ||!verified.hasSection(0xB000)||verified.hasEmbeddedAiGraph()||verified.hasSection(0x1E00))return fail("Generated MOB failed validation: "+report.toText());
        const auto layout=p.worldDocument.loadEditorWorld(p.profileId,p.mapRoots);
        if(layout.storageBlocked)return fail(layout.warning);
        for(const auto& map:layout.zones)if(key(p.worldDocument.absoluteResourcePath(map.mprPath))==key(path)) {
            if(!map.mobPath.isEmpty()&&key(p.worldDocument.absoluteResourcePath(map.mobPath))!=key(p.destination))return fail("World map already refers to a different primary MOB");
            if(map.mobPath.isEmpty()){SWorldMapLayoutChange change;change.before=change.after=map;change.after.mobPath=p.worldDocument.portableResourcePath(p.destination);p.layoutChanges.append(change);}
        }
        return p;
    }
    auto layout=p.worldDocument.loadEditorWorld(p.profileId,p.mapRoots);
    if(layout.storageBlocked||!layout.warning.isEmpty())return fail(layout.warning);
    auto maps=layout.zones;int selected=-1,neighbor=-1;
    const QString relativeName=args["relativeTo"].toString();const auto nearPath=resolve(p,relativeName,"mpr");
    for(int i=0;i<maps.size();++i){const auto full=p.worldDocument.absoluteResourcePath(maps[i].mprPath);
        if(key(full)==key(path))selected=i;
        if(!relativeName.isEmpty()&&(maps[i].zoneId.compare(relativeName,Qt::CaseInsensitive)==0||(!nearPath.isEmpty()&&key(full)==key(nearPath)))){
            if(neighbor>=0)return fail("Ambiguous relativeTo map");neighbor=i;}}
    SWorldMapZoneOrigin map;
    if(selected>=0)map=maps[selected];else {
        map.profileId=p.profileId;map.zoneId=QFileInfo(path).completeBaseName();map.displayName=map.zoneId;map.mprIdentity=map.zoneId;map.componentAnchorZoneId=map.zoneId;
        map.mprPath=p.worldDocument.portableResourcePath(path);
        const auto mob=QFileInfo(path).dir().filePath(map.zoneId+".mob");if(authorized(mob,p.mapRoots))map.mobPath=p.worldDocument.portableResourcePath(mob);
        map.allodId=args["world"].toString();
    }
    map.sectorWidth=doc.mapInfo().sectorCountX;map.sectorHeight=doc.mapInfo().sectorCountY;
    if(!relativeName.isEmpty()) {
        if(neighbor<0||neighbor==selected)return fail("relativeTo must name a different placed profile map");
        if(selected>=0&&map.allodId!=maps[neighbor].allodId)return fail("Moving an existing map between worlds is not supported");
        if(selected<0)map.componentAnchorZoneId=maps[neighbor].componentAnchorZoneId;
        CMprDocument n;const auto npath=p.worldDocument.absoluteResourcePath(maps[neighbor].mprPath);
        if(!authorized(npath,p.mapRoots)||!readMpr(npath,&n,&error))return fail("Cannot verify neighbour: "+error);
        p.fileHashes[npath]=digest(npath);map.allodId=maps[neighbor].allodId;map.origin=maps[neighbor].origin;
        const auto side=args["side"].toString();
        if(side=="E")map.origin.rx()+=n.mapInfo().sectorCountX*32;
        else if(side=="W")map.origin.rx()-=map.sectorWidth*32;
        else if(side=="N")map.origin.ry()+=n.mapInfo().sectorCountY*32;
        else if(side=="S")map.origin.ry()-=map.sectorHeight*32;
        else return fail("relativeTo requires side N/S/E/W");
        if(args.contains("x")||args.contains("y")||args.contains("world"))return fail("Choose relativeTo/side OR x/y/world");
    }else {
        if(!args.contains("x")||!args.contains("y")||args.contains("side"))return fail("Supply x/y and world for a new map, or relativeTo/side");
        if(selected>=0&&args.contains("world")&&map.allodId!=args["world"].toString())return fail("Moving an existing map between worlds is not supported");
        map.origin={args["x"].toDouble(),args["y"].toDouble()};
    }
    if(map.allodId.isEmpty())return fail("New coordinate placement requires world");
    map.provenance=EWorldMapOriginProvenance::Manual;
    const QRectF bounds(map.origin,QSizeF(map.sectorWidth*32,map.sectorHeight*32));
    for(int i=0;i<maps.size();++i)if(i!=selected&&maps[i].allodId==map.allodId){
        const auto otherPath=p.worldDocument.absoluteResourcePath(maps[i].mprPath);CMprDocument other;
        if(!authorized(otherPath,p.mapRoots)||!readMpr(otherPath,&other,&error))return fail("Cannot check world intersections: "+error);
        p.fileHashes[otherPath]=digest(otherPath);
        const auto intersection=bounds.intersected(QRectF(maps[i].origin,QSizeF(other.mapInfo().sectorCountX*32,other.mapInfo().sectorCountY*32)));
        if(intersection.width()>1e-6&&intersection.height()>1e-6)return fail("Placement overlaps map "+maps[i].zoneId);
    }
    SWorldMapLayoutChange change;change.hadBefore=selected>=0;if(change.hadBefore)change.before=maps[selected];change.after=map;p.layoutChanges.append(change);
    p.summary={{"origin",QJsonArray{map.origin.x(),map.origin.y()}},{"world",map.allodId},{"zone",map.zoneId}};
    return cancel->load()?fail("Cancelled; unchanged"):p;
}

class CAiLifecycleCommand : public QUndoCommand {
public:
    QPointer<MainWindow> w;QVector<SWorldMapLayoutChange> changes;QString layoutPath,profile,mobPath,error;bool first=true;
    CAiLifecycleCommand(MainWindow* window):w(window){}
    void run(bool forward) {
        if(!w)return;
        error.clear();
        if(!changes.isEmpty()){
            if(w->m_worldMapLayoutDocument.path()!=layoutPath||w->activeResourceProfile().profileId!=profile)error="World profile changed";
            else if(w->m_worldMapLayoutDocument.applyChanges(changes,forward,&error))w->refreshEditorWorld();
        }
        if(error.isEmpty()&&!mobPath.isEmpty()) {
            QScopedValueRollback<QUndoStack*> history(w->view()->m_pUndoStack,nullptr);
            if(forward){QFileInfo file(mobPath);w->view()->loadMob(file);}else {
                QString name;for(auto* mob:w->view()->m_aMob)if(key(mob->filePath().absoluteFilePath())==key(mobPath))name=mob->mobName();
                if(!name.isEmpty())w->view()->unloadMob(name);
            }
        }
        if(!error.isEmpty()){w->setProperty("aiLifecycleHistoryError",error);setObsolete(true);}
    }
    void redo()override{if(first){first=false;return;}run(true);}
    void undo()override{run(false);}
};
bool CAiEditAdapter::lifecycleCommand(const QUndoCommand* c){return dynamic_cast<const CAiLifecycleCommand*>(c)!=nullptr;}
bool CAiEditAdapter::lifecycleWrites(const QUndoCommand* c){const auto* command=dynamic_cast<const CAiLifecycleCommand*>(c);return command&&!command->changes.isEmpty();}

QJsonObject CAiEditAdapter::applyLifecycle(MainWindow* w,const ai::EditPlan& p) {
    auto* v=w->view();
    if((p.method=="open_map"||p.method=="close_map")&&dirty(w))return ai::error("Unsaved changes: save first; workspace unchanged");
    for(auto it=p.fileHashes.begin();it!=p.fileHashes.end();++it)if(digest(it.key())!=it.value())return ai::error("Profile file changed during preparation; retry: "+it.key());
    if(p.method=="close_map") {w->closeAll();if(v->m_pLand->isMprLoad()||!v->m_aMob.isEmpty())return ai::error("Workspace close refused; documents retained");return {{"status","closed"},{"revision",revision(w)}};}
    QString targetError;auto targetLease=CProcessEditLock::acquire(p.destination,&targetError);
    if(!targetLease)return ai::error(targetError);
    if(p.method=="open_map") {
        const auto previous=v->m_pLand->isMprLoad()?v->m_pLand->filePath().absoluteFilePath():QString();QStringList mobs;for(auto* m:v->m_aMob)mobs.append(m->filePath().absoluteFilePath());
        QString leaseError;auto mapLease=CProcessEditLock::acquire(p.destination,&leaseError);
        auto mobLease=p.mobPath.isEmpty()?mapLease:CProcessEditLock::acquire(p.mobPath,&leaseError);
        if(!mapLease||!mobLease)return ai::error(leaseError);
        // Navigation is read-only even in a human editor. It uses exactly the
        // pair validated by prepare(), without persisting discovery or changing
        // lifecycle Undo's empty-binding baseline.
        const QString zone=QFileInfo(p.destination).completeBaseName();
        w->m_preferredEditorActiveMap=zone;
        w->openMapPairWithCameraPolicy(p.destination,p.mobPath,p.mobPath.isEmpty()?QString():zone,
                                       !p.mobPath.isEmpty(),QPointF(),QPointF(),false);
        if(!v->m_pLand->isMprLoad()||key(v->m_pLand->filePath().absoluteFilePath())!=key(p.destination)||!v->ensureTerrainAutoCatalogReady()) {
            v->unloadMob("");v->unloadLand(false);if(!previous.isEmpty()){w->replaceLoadedLandscape(QFileInfo(previous),false);for(const auto& m:mobs){QFileInfo file(m);v->loadMob(file);}}
            return ai::error("Open failed; previous clean workspace restored");
        }
        w->refreshEditorWorld();return {{"status","opened"},{"path",p.destination},{"revision",revision(w)}};
    }
    if(p.method=="create_mob") {
        if(QFileInfo::exists(p.destination)||QFileInfo(p.destination).isSymLink())return ai::error("MOB already exists; never overwrite");
        if(!authorized(QFileInfo(p.destination).absolutePath()+"/"+QFileInfo(p.activePath).fileName(),w->activeMapRoots().toList()))return ai::error("Profile root changed");
        SMapSaveFile file;file.path=p.destination;file.existed=false;file.after=p.mobBytes;
        const auto result=commitMapSaveBatch({file});if(!result.success)return ai::error(result.error);
        QFileInfo mob(p.destination);v->loadMob(mob);
        bool found=false;for(auto* loaded:v->m_aMob)found|=key(loaded->filePath().absoluteFilePath())==key(p.destination);
        if(!found){const bool removed=digest(p.destination)==QCryptographicHash::hash(p.mobBytes,QCryptographicHash::Sha256)&&QFile::remove(p.destination);
            return ai::error(removed?"MOB loading failed; new file rolled back":"MOB loading failed; rollback incomplete, inspect "+p.destination);}
        QString error;
        if(!p.layoutChanges.isEmpty()&&!w->m_worldMapLayoutDocument.applyChanges(p.layoutChanges,true,&error)) {
            {QScopedValueRollback<QUndoStack*> history(v->m_pUndoStack,nullptr);for(auto* m:v->m_aMob)if(key(m->filePath().absoluteFilePath())==key(p.destination)){const auto name=m->mobName();v->unloadMob(name);break;}}
            const bool removed=digest(p.destination)==QCryptographicHash::hash(p.mobBytes,QCryptographicHash::Sha256)&&QFile::remove(p.destination);
            return ai::error((removed?QString("MOB/layout transaction rolled back: "):QString("MOB/layout rollback incomplete; inspect ")+p.destination+": ")+error);
        }
        auto* c=new CAiLifecycleCommand(w);c->setText("ИИ: создать основной MOB");c->mobPath=p.destination;c->changes=p.layoutChanges;c->layoutPath=p.worldDocument.path();c->profile=p.profileId;w->m_undoStack->push(c);
        if(!p.layoutChanges.isEmpty())w->refreshEditorWorld();
        w->refreshMapTestContext(false,false);return {{"status","applied"},{"path",p.destination},{"revision",revision(w)}};
    }
    QString error;
    if(w->m_worldMapLayoutDocument.path()!=p.worldDocument.path())return ai::error("World profile changed; retry");
    // Commit through the same compare/backup/atomic-write contract as manual placement.
    if(!w->m_worldMapLayoutDocument.applyChanges(p.layoutChanges,true,&error))return ai::error(error);
    auto* c=new CAiLifecycleCommand(w);c->setText("ИИ: положение карты в мире");c->changes=p.layoutChanges;c->layoutPath=p.worldDocument.path();c->profile=p.profileId;w->m_undoStack->push(c);
    w->refreshEditorWorld();auto result=p.summary;result["status"]="applied";result["revision"]=revision(w);return result;
}
