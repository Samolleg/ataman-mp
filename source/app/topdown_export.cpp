#include "topdown_export.h"
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "object_base.h"
#include "unit.h"
#include "resourcemanager.h"
#include "resource_name.h"
#include "ai_agent_edit.h"
#include "settings.h"
#include <QtConcurrent>
#include <QOpenGLFunctions>

namespace {
struct Pass { const char* file; ETopdownPass pass; };
const Pass passes[]={{"composite.png",ETopdownPass::Composite},{"terrain.png",ETopdownPass::Terrain},
    {"relief.png",ETopdownPass::Relief},{"water.png",ETopdownPass::Water},{"water-mask.png",ETopdownPass::WaterMask},
    {"structures.png",ETopdownPass::Structures},{"structures-mask.png",ETopdownPass::StructuresMask},{"vegetation.png",ETopdownPass::Vegetation}};
QJsonArray vector(const QVector3D& p){return {p.x(),p.y(),p.z()};}
QJsonObject savedFile(const QString& path,const QByteArray& baseline,bool dirty) {
    QFileInfo f(path);QFile file(path);QCryptographicHash hash(QCryptographicHash::Sha256);
    const bool exists=file.open(QIODevice::ReadOnly)&&hash.addData(&file);
    return {{"path",f.absoluteFilePath()},{"exists",exists},{"bytes",double(f.size())},
        {"diskSha256",exists?QString::fromLatin1(hash.result().toHex()):QString()},
        {"loadedBaselineSha256",QString::fromLatin1(QCryptographicHash::hash(baseline,QCryptographicHash::Sha256).toHex())},
        {"unsavedChanges",dirty},{"identityKind","saved source and loaded baseline; live scene identified separately by snapshot"}};
}
}
CTopdownExport::CTopdownExport(MainWindow* w,const QJsonObject& args):m_window(w),m_view(w?w->view():nullptr),m_args(args){}
CTopdownExport::~CTopdownExport(){m_cancel->store(true);releaseScene();}
void CTopdownExport::begin(){m_clock.start();QTimer::singleShot(0,this,[this](){step();});}
void CTopdownExport::releaseScene(){
    if(m_view && !m_parts.isEmpty())m_view->makeCurrent();
    m_structureItems.clear();m_vegetationItems.clear();m_parts.clear();m_textures.clear();
}
void CTopdownExport::fail(QString code,QString message,QJsonObject details) {
    if(done())return;
    m_cancel->store(true);m_terminal=code=="CANCELLED"?"cancelled":"failed";
    details["code"]=code;details["error"]=message;details["phase"]=m_phase;
    details["deadlineMs"]=120000;details["elapsedMs"]=double(m_clock.elapsed());
    if(!m_temporary.isEmpty()){details["incompletePath"]=m_temporary;details["incomplete"]=true;}
    m_result=details;m_images.clear();releaseScene();m_outputLease.reset();if(finished)finished();
}
void CTopdownExport::cancel(const QString& code){fail(code,code=="EXPORT_TIMEOUT"?"Export exceeded its 120 second deadline":"Export cancelled; documents unchanged");}
QByteArray CTopdownExport::sceneIdentity() const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(m_revision.toUtf8());
    hash.addData(m_view->m_activeMob?m_view->m_activeMob->filePath().absoluteFilePath().toUtf8():QByteArray());
    hash.addData(m_view->m_environmentMob?m_view->m_environmentMob->filePath().absoluteFilePath().toUtf8():QByteArray());
    for(auto* mob:m_view->m_aMob) {
        hash.addData(mob->filePath().absoluteFilePath().toUtf8());
        auto world=mob->worldSet();QJsonObject environment;world.serializeJson(environment);
        hash.addData(QJsonDocument(environment).toJson(QJsonDocument::Compact));
        for(auto* node:mob->nodes()) {
            hash.addData(QJsonDocument(node->toJson()).toJson(QJsonDocument::Compact));
            hash.addData(node->isMarkDeleted()?"deleted":"present");
            hash.addData(QJsonDocument(QJsonObject{{"drawPosition",vector(node->drawPosition())}}).toJson(QJsonDocument::Compact));
        }
    }
    hash.addData(m_catalog.stagedBytes());
    return hash.result();
}
bool CTopdownExport::unchanged() {
    if(!m_window||!m_view) {fail("REVISION_CONFLICT","Source document closed");return false;}
    if(CAiEditAdapter::revision(m_window)!=m_revision || sceneIdentity()!=m_sceneIdentity) {
        fail("REVISION_CONFLICT","Map/scene changed during export; read revision and retry");return false;
    }
    for(const auto& value:m_resources) {
        const auto r=value.toObject();if(r["provider"].toString().isEmpty())continue;
        QFileInfo f(r["provider"].toString());
        if(!f.exists()||double(f.size())!=r["providerBytes"].toDouble()||double(f.lastModified().toMSecsSinceEpoch())!=r["providerModifiedMs"].toDouble()) {
            fail("REVISION_CONFLICT","Resource provider changed during export",{{"resource",r}});return false;
        }
    }
    return true;
}
bool CTopdownExport::initialize() {
    if(!m_view||!m_view->m_pLand||!m_view->m_pLand->isMprLoad()){fail("NO_MAP","Load an MPR in the selected instance before export");return false;}
    m_revision=CAiEditAdapter::revision(m_window);
    if(m_args["revision"].toString()!=m_revision){fail("REVISION_CONFLICT","Stale revision; call get_revision before export");return false;}
    if(CAiEditAdapter::busy(m_window)){fail("REVISION_CONFLICT","Scene interaction/load is in progress; retry when ready");return false;}
    QStringList forbidden=CAiReadAdapter::mapRoots(m_window)+configuredResourceSourcePaths(CObjectList::getInstance()->settings());
    const auto* game=dynamic_cast<const COptString*>(m_view->m_pSettings->opt("gameRoot"));if(game&&!game->value().isEmpty())forbidden.append(game->value());
    for(auto* mob:m_view->m_aMob)forbidden.append(mob->filePath().absolutePath());
    forbidden.append(m_view->m_pLand->filePath().absolutePath());
    m_forbidden=forbidden;
    const auto output=ai::validateTopdownOutput(m_args["outputDir"].toString(),ai::topdownWorkspaceRoot(),forbidden);
    if(output.contains("error")){fail(output["code"].toString(),output["error"].toString());return false;}
    m_output=output["path"].toString();
    QString leaseError;m_outputLease=ai::lockTopdownParent(m_output,output["workspace"].toString(),&leaseError);
    if(!m_outputLease){fail("OUTPUT_PATH_ERROR",leaseError);return false;}
    const auto lockedPath=ai::validateTopdownOutput(m_output,output["workspace"].toString(),m_forbidden);
    if(lockedPath.contains("error")){fail(lockedPath["code"].toString(),lockedPath["error"].toString());return false;}
    const int ppu=m_args["pixelsPerWorldUnit"].toInt(4);
    const auto w=m_view->m_pLand->widthInWorldUnits(),h=m_view->m_pLand->heightInWorldUnits();
    if(!QList<int>{1,2,4}.contains(ppu)||!w||!h||quint64(w)*h*ppu*ppu>8*1024*1024) {
        fail("EXPORT_SIZE_LIMIT","Export limit is 8,388,608 pixels per layer; pixelsPerWorldUnit must be 1, 2 or 4");return false;
    }
    m_size=QSize(int(w)*ppu,int(h)*ppu);
    if(!m_view->isValid()){fail("RENDER_UNAVAILABLE","Owning viewport renderer is unavailable");return false;}
    m_view->makeCurrent();auto* context=QOpenGLContext::currentContext();GLint limit=0;
    if(context)context->functions()->glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
    if(!context||!limit){fail("RENDER_UNAVAILABLE","Owning GL context is unavailable");return false;}
    if(m_size.width()>limit||m_size.height()>limit){fail("EXPORT_SIZE_LIMIT","Requested size exceeds GPU texture limit",{{"maximumTextureSize",limit}});return false;}
    auto* textures=CTextureList::getInstance();
    const auto terrainSources=textures->textureSources(m_view->m_pLand->captureSourceAtlas());
    // Live atlas previews are owned by the terrain edit document; their
    // snapshot/revision is their identity rather than a fictitious disk hash.
    const bool preview=m_view->m_pLand->captureUsesPreviewAtlas();
    if(terrainSources.size()!=int(m_view->m_pLand->textureNum())) {
        fail("MISSING_RESOURCES","Terrain atlas has missing/unsupported pages or a fallback texture",
             {{"mapNamespace",m_view->m_pLand->mapName()},{"requiredPages",QJsonArray::fromStringList(ei::terrainTextureResourceNames(m_view->m_pLand->mapName(),int(m_view->m_pLand->textureNum())))},{"resolvedPages",terrainSources}});return false;
    }
    m_resources=terrainSources;
    if(preview)m_resources.append(QJsonObject{{"kind","live terrain working atlas"},{"revision",m_revision},{"provider",QJsonValue::Null}});
    const auto db=ei::ProfileDatabaseContext::instance().current();
    m_catalogPath=ei::TechnicalModelCatalog::pathForDatabase(db?db->sourceArchivePath:QString());
    if(!QFileInfo::exists(m_catalogPath))m_catalogPath=QCoreApplication::applicationDirPath()+"/model-catalog.json";
    QString error;if(!m_catalog.load(m_catalogPath,&error)){fail("UNSUPPORTED_GEOMETRY","Cannot read author model categories: "+error);return false;}
    if(QFileInfo::exists(m_catalogPath)) {
        QFile f(m_catalogPath);f.open(QIODevice::ReadOnly);const auto bytes=f.readAll();QFileInfo info(m_catalogPath);
        m_resources.append(QJsonObject{{"member","model categories"},{"provider",info.canonicalFilePath()},
            {"providerBytes",double(info.size())},{"providerModifiedMs",double(info.lastModified().toMSecsSinceEpoch())},
            {"sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}});
    }
    QJsonArray mobs;
    for(auto* mob:m_view->m_aMob) {
        mobs.append(savedFile(mob->filePath().absoluteFilePath(),mob->originalData(),mob->isDirty()));
        for(auto* node:mob->nodes()){m_nodes.append(node);m_mobPaths.append(mob->filePath().absoluteFilePath());}
    }
    const auto* profile=dynamic_cast<const COptString*>(m_view->m_pSettings->opt("resourceProfile"));
    m_sceneIdentity=sceneIdentity();
    m_manifest={{"schemaVersion",2},{"exportId",QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"retention",QJsonObject{{"policy","until owner deletes package or registration; no operation TTL"},{"viewer","read_export_image"},{"bridgeReconnect",true}}},
        {"cartographicBase",QJsonObject{{"status","limited"},{"reason","No authored road semantics; roads must be inspected in terrain/composite"},
            {"sources",QJsonArray{"relief.png","water-mask.png","structures-mask.png"}},{"geometry","exact pass pixels; outlines remain inside masks; no relocation or inferred geography"}}},
        {"exporter",QJsonObject{{"name","EI Mapper Ataman"},{"version",ENDURANCE_VERSION},{"method","export_map_topdown"}}},
        {"profile",profile?profile->value():QString()},{"map",m_view->m_pLand->filePath().baseName()},
        {"mpr",savedFile(m_view->m_pLand->filePath().absoluteFilePath(),m_view->m_mprDocument.originalBytes(),m_view->m_pLand->isDirty()||m_view->m_terrainEdit.isDirty())},
        {"mobs",mobs},{"revision",m_revision},{"snapshot",QString::fromLatin1(m_sceneIdentity.toHex())},
        {"unsavedChanges",CAiEditAdapter::dirty(m_window)},{"worldBounds",QJsonArray{0,0,double(w),double(h)}},
        {"width",m_size.width()},{"height",m_size.height()},{"pixelsPerWorldUnit",ppu},{"pixelSizeWorld",QJsonArray{1.0/ppu,1.0/ppu}},
        {"worldToPixel",QJsonArray{QJsonArray{ppu,0,0},QJsonArray{0,-ppu,m_size.height()},QJsonArray{0,0,1}}},
        {"pixelToWorld",QJsonArray{QJsonArray{1.0/ppu,0,0},QJsonArray{0,-1.0/ppu,double(h)},QJsonArray{0,0,1}}},
        {"coordinates",QJsonObject{{"origin","top-left PNG"},{"x","world +X right"},{"y","world +Y up; no geographical north inferred"},{"transform","pixel boundaries; pixel centers use (i+0.5,j+0.5)"}}},
        {"nativeConversion",QJsonObject{{"capture","GL bottom-left to QImage top-left exactly once"},{"gameImage","not used; minimap_bake applies its separate game Y convention"}}},
        {"roadsMask",QJsonObject{{"status","unavailable"},{"reason","No unambiguous authored road semantic mask in the active renderer material contract"},{"source","MPR tile atlas/materials; actual textures and transitions rendered in terrain/composite"}}},
        {"parameters",QJsonObject{{"deadlineMs",120000},{"maximumPixelsPerLayer",8*1024*1024},{"maximumTextureSize",limit},{"pose","static assembled/rest pose, time 0; overlay motion disabled"},{"neighbors",false},{"editorHiddenObjects",true},{"vegetationCategories",QJsonArray{"trees","bushes"}},{"categorySource",m_catalogPath},{"waterMask","native overlay geometry depth-tested against terrain only; independent of texture opacity"}}},
        {"warnings",QJsonArray{"Road semantic mask unavailable; road materials are present in textured passes.","Cartographic base readability is limited: no independently isolated roads; compare terrain/composite."}}};
    return unchanged();
}
bool CTopdownExport::resolveObject() {
    const int index=m_next++;auto* node=m_nodes[index].data();
    if(!node){fail("REVISION_CONFLICT","Object removed while resolving resources");return false;}
    const QString model=node->modelName().toLower();
    const auto category=m_catalog.category(model);
    const bool nativePlant=model.startsWith("nafltr")||model.startsWith("naflbu")||model.startsWith("naflte")||model.startsWith("naflli");
    // Installed profile's legacy tree assembly: actual MOD parts are trunk,
    // branches, crown (sammodels provider). The old catalog lacks its category.
    const bool legacyPlant=model=="samt56z1";
    const bool plant=category=="trees"||category=="bushes"||nativePlant||legacyPlant;
    QJsonObject entry{{"sourceMob",m_mobPaths[index]},{"id",double(node->mapId())},{"nodeType",int(node->nodeType())},
        {"model",model},{"texture",node->textureName()},{"category",plant?"vegetation":"structures"},{"authorCategory",category},{"position",vector(node->position())},
        {"rotation",QJsonArray{node->rotation().scalar(),node->rotation().x(),node->rotation().y(),node->rotation().z()}},{"complexion",vector(node->complection())},
        {"editorHidden",node->nodeState()==eHidden},{"meshParts",0},
        {"classificationSource",nativePlant?"native nature/flora resource family":legacyPlant?"legacy samt56z1 MOD hierarchy: trunk/branches/crown":m_catalogPath}};
    QString excluded;
    if(node->isMarkDeleted())excluded="deleted in current document";
    else if(dynamic_cast<CUnit*>(node))excluded="dynamic unit/NPC/animal";
    else if(node->nodeType()==eLight||node->nodeType()==eSound||node->nodeType()==eParticle)excluded="editor helper/light/sound/particle";
    else if(node->nodeType()==eMagicTrap)excluded="magic-trap logic/effect marker; renderer uses the internal trap helper";
    else if(node->nodeType()!=eBaseType&&node->nodeType()!=eWorldObject&&node->nodeType()!=eTorch&&node->nodeType()!=eMagicTrap&&node->nodeType()!=eLever)excluded="editor/logic marker";
    if(!excluded.isEmpty()){entry["included"]=false;entry["reason"]=excluded;entry["category"]="excluded";m_objects.append(entry);++m_excluded;return true;}
    auto* object=dynamic_cast<CObjectBase*>(node);
    auto* models=CObjectList::getInstance();QString resource=model+".mod";
    auto* figure=models->getFigureResource(resource);
    if(!figure){resource=model+".fig";figure=models->getFigureResource(resource);}
    const auto bad=[&](QString code,QString reason){entry["reason"]=reason;fail(code,reason,{{"object",entry},{"resource",resource}});return false;};
    if(!object||!figure)return bad("MISSING_RESOURCES","Placed object model unavailable (no placeholders): "+model);
    if(!figure->completeCaptureGeometry())return bad("UNSUPPORTED_GEOMETRY","Incomplete FIG vertex/component data: "+model);
    if(!models->figureProblems(resource).isEmpty())return bad("UNSUPPORTED_GEOMETRY",models->figureProblems(resource).join("; "));
    auto modelSources=models->figureSources(resource);
    if(modelSources.isEmpty())return bad("MISSING_RESOURCES","Model provider identity unavailable: "+resource);
    // Resolve through the usual loader before collecting. It is synchronous for
    // static MOB visuals, unlike the camera-prioritized dynamic-unit resolver.
    m_view->makeCurrent();
    auto items=object->staticRenderItems();
    if(items.isEmpty()){node->loadFigure();node->loadTexture();items=object->staticRenderItems();}
    if(items.isEmpty())return bad("UNSUPPORTED_GEOMETRY","No drawable mesh parts for placed object: "+model);
    const auto addResources=[&](const QJsonArray& resources)->bool {
        for(const auto& value:resources){const auto r=value.toObject();const QString key=r["provider"].toString()+"|"+r["member"].toString()+"|"+r["sha256"].toString();
            if(!m_resourceKeys.contains(key)){
                QString provider;const auto bytes=models->resourceData(r["member"].toString(),&provider);
                if(bytes.isEmpty())return bad("MISSING_RESOURCES","Winning provider cannot resolve "+r["member"].toString());
                if(QFileInfo(provider).canonicalFilePath().compare(r["provider"].toString(),Qt::CaseInsensitive)!=0
                    ||QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())!=r["sha256"].toString())
                    return bad("REVISION_CONFLICT","Loaded visual and selected profile resource identity differ: "+r["member"].toString());
                m_resourceKeys.insert(key);m_resources.append(r);
            }}
        return true;
    };
    if(!addResources(modelSources))return false;QJsonArray partNames;
    for(auto item:items) {
        auto* textures=CTextureList::getInstance();const auto sources=textures->textureSources(item.texture);
        if(!renderTextureReady(item.texture))return bad("MISSING_RESOURCES","Texture missing/unsupported for "+model+" / "+item.part->name());
        if(sources.isEmpty())return bad("MISSING_RESOURCES","Texture resource identity unavailable for "+model+" / "+item.part->name());
        if(!addResources(sources))return false;
        for(const auto& vertex:item.part->vertData()) {
            for(float coordinate:{vertex.position.x(),vertex.position.y(),vertex.position.z(),vertex.normal.x(),vertex.normal.y(),vertex.normal.z(),vertex.texCoord.x(),vertex.texCoord.y()})
                if(!qIsFinite(coordinate))return bad("UNSUPPORTED_GEOMETRY","Non-finite mesh vertex: "+model+" / "+item.part->name());
        }
        auto clone=QSharedPointer<CPart>::create(*item.part);m_parts.append(clone);
        m_textures.append(textures->retainSceneTexture(item.texture));
        item.part=clone.data();item.highlighted=false;item.castsShadow=false;
        if(plant)m_vegetationItems.append(item);else m_structureItems.append(item);
        partNames.append(item.part->name());
    }
    entry["included"]=true;entry["reason"]="all authored drawable parts; terrain depth may fully occlude geometry";
    entry["meshParts"]=items.size();entry["parts"]=partNames;entry["modelResources"]=modelSources;
    if(category.isEmpty())entry["classification"]="uncategorized static geometry retained as structures";
    m_objects.append(entry);if(plant)++m_vegetation;else ++m_structures;
    return true;
}
void CTopdownExport::step() {
    if(done())return;
    try {
        if(m_clock.elapsed()>=120000){cancel("EXPORT_TIMEOUT");return;}
        if(m_revision.isEmpty()){if(!initialize())return;}
        else if(!unchanged())return;
        if(m_phase=="resolving_resources") {
            QElapsedTimer slice;slice.start();
            while(m_next<m_nodes.size()&&slice.elapsed()<12)if(!resolveObject())return;
            if(m_next==m_nodes.size()) {
                m_manifest["objects"]=m_objects;m_manifest["resources"]=m_resources;
                m_manifest["resourcesIdentity"]=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(m_resources).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
                m_manifest["counts"]=QJsonObject{{"objects",m_nodes.size()},{"structures",m_structures},{"vegetation",m_vegetation},{"excluded",m_excluded},{"meshParts",m_parts.size()},{"resources",m_resources.size()}};
                m_phase="capturing";
            }
        } else if(m_phase=="capturing") {
            const auto pass=passes[m_pass];QString error;
            const auto& items=pass.pass==ETopdownPass::Vegetation?m_vegetationItems:
                (pass.pass==ETopdownPass::Composite||pass.pass==ETopdownPass::Structures||pass.pass==ETopdownPass::StructuresMask)?m_structureItems:QVector<SRenderItem>();
            auto image=m_view->captureTopdownPass(pass.pass,m_size,items,&error);
            if(image.isNull()){fail(error.startsWith("EXPORT_SIZE_LIMIT")?"EXPORT_SIZE_LIMIT":"RENDER_UNAVAILABLE",error,{{"pass",pass.file}});return;}
            m_images.insert(pass.file,image);++m_pass;
            if(m_pass==8) {
                if(!unchanged())return;
                m_phase="writing";
                m_temporary=m_output+".incomplete-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
                if(!QDir().mkdir(m_temporary)){fail("OUTPUT_PATH_ERROR","Cannot create export staging directory");return;}
                const auto route=ai::validateTopdownOutput(m_output,ai::topdownWorkspaceRoot(),m_forbidden);
                if(route.contains("error")){fail(route["code"].toString(),route["error"].toString());return;}
                const auto images=m_images;const auto manifest=m_manifest;const auto temporary=m_temporary;const auto cancel=m_cancel;const auto lease=m_outputLease;
                m_writer=new QFutureWatcher<QJsonObject>(this);
                m_writer->setFuture(QtConcurrent::run([images,manifest,temporary,cancel,lease](){Q_UNUSED(lease);return ai::writeTopdownImages(images,temporary,manifest,cancel);}));
                m_images.clear();releaseScene();
            }
        } else if(m_phase=="writing" && m_writer->isFinished()) {
            auto result=m_writer->result();m_writer->deleteLater();m_writer=nullptr;
            if(result.contains("error")){fail("OUTPUT_PATH_ERROR",result["error"].toString());return;}
            if(!unchanged())return;
            if(m_clock.elapsed()>=120000){cancel("EXPORT_TIMEOUT");return;}
            // Revalidate the route at publication; never overwrite a newly
            // created destination or follow a substituted junction.
            const auto path=ai::validateTopdownOutput(m_output,ai::topdownWorkspaceRoot(),m_forbidden);
            if(path.contains("error")){fail(path["code"].toString(),path["error"].toString());return;}
            const QString manifestPath=QDir(m_temporary).filePath("manifest.json");
            QFile input(manifestPath);if(!input.open(QIODevice::ReadOnly)){fail("OUTPUT_PATH_ERROR","Cannot read prepared manifest");return;}
            auto manifest=QJsonDocument::fromJson(input.readAll()).object();input.close();manifest["status"]="complete";
            QSaveFile finalManifest(manifestPath);
            if(!finalManifest.open(QIODevice::WriteOnly)||finalManifest.write(QJsonDocument(manifest).toJson())<0||!finalManifest.commit()) {
                fail("OUTPUT_PATH_ERROR","Cannot finalize manifest");return;
            }
            QString registrationError;
            if(!ai::registerTopdownExport(QDir(m_output).filePath("manifest.json"),manifest["exportId"].toString(),
                QCryptographicHash::hash(QJsonDocument(manifest).toJson(),QCryptographicHash::Sha256).toHex(),&registrationError)) {
                QFile::remove(manifestPath);fail("OUTPUT_PATH_ERROR",registrationError);return;
            }
            if(!QDir().rename(m_temporary,m_output)){
                QFile::remove(manifestPath); // only our unpublished manifest
                fail("OUTPUT_PATH_ERROR","Atomic export publication failed; incomplete directory retained");return;
            }
            QJsonObject paths;const auto names=result["files"].toObject();
            for(auto it=names.begin();it!=names.end();++it)paths[it.key()]=QDir(m_output).filePath(it.key());
            paths["manifest.json"]=QDir(m_output).filePath("manifest.json");result["files"]=paths;
            result["manifestPath"]=paths["manifest.json"];result["counts"]=m_manifest["counts"];result["warnings"]=m_manifest["warnings"];
            result["exportId"]=m_manifest["exportId"];result["cartographicBase"]=m_manifest["cartographicBase"];result["retention"]=m_manifest["retention"];
            result["revision"]=m_revision;result["deadlineMs"]=120000;result["elapsedMs"]=double(m_clock.elapsed());
            m_result=result;m_terminal="complete";m_outputLease.reset();if(finished)finished();return;
        }
        QTimer::singleShot(1,this,[this](){step();});
    } catch(const std::exception& e){fail("RENDER_UNAVAILABLE",QString::fromUtf8(e.what()));}
      catch(...){fail("RENDER_UNAVAILABLE","Export failed with an unexpected exception; render state restored");}
}
