#include "terrain_hd_variant_bank.h"
#include "terrain_hd_library.h"
#include "terrain_hd_republish.h"
#include "terrain_hd_provenance.h"
#include "terrain_virtual_atlas.h"
#include "res_archive_index.h"
#include "mpr_document.h"
#include "mmp_codec.h"
#include "endurance_document.h"
#include <QDirIterator>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <algorithm>
#include <cstring>
#include <QFile>
#include <QTemporaryDir>
#include <QRegularExpression>
#include "types.h"

namespace TerrainHdVariantBank {
namespace {
QMutex mutex;QSharedPointer<Bank> cached;QString cachedRoot;
QByteArray read(const QString& path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}
QByteArray sha(const QByteArray& data){return QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex();}
bool save(const QString& path,const QByteArray& bytes){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
QString inputPath(const QString& path,const QString& libraryRoot) {
    if(!path.startsWith("@library/"))return path;
    const auto relative=path.mid(9);
    if(relative.contains("..")||QDir::isAbsolutePath(relative)||relative.contains('\\'))return {};
    return QDir(libraryRoot).filePath(relative);
}
void localInputs(QJsonObject* manifest,const QString& libraryRoot) {
    QJsonArray inputs;
    for(const auto& value:(*manifest)["inputs"].toArray()) {
        auto input=value.toObject();const auto path=input["path"].toString();
        const auto relative=QDir(libraryRoot).relativeFilePath(path);
        if(!relative.startsWith("..")&&!QDir::isAbsolutePath(relative))input["path"]="@library/"+relative;
        inputs.append(input);
    }
    (*manifest)["inputs"]=inputs;
}
bool current(const QJsonObject& manifest,const QString& libraryRoot={}){
    const auto inputs=manifest["inputs"].toArray();if(inputs.isEmpty())return false;
    QMap<QString,QVector<QJsonObject>> byArchive;
    for(const auto& value:inputs){const auto input=value.toObject();
        if(input["path"].toString().isEmpty()||input["member"].toString().isEmpty()
           ||input["sha256"].toString().size()!=64)return false;
        const auto path=inputPath(input["path"].toString(),libraryRoot);if(path.isEmpty())return false;
        byArchive[path].append(input);}
    for(auto it=byArchive.cbegin();it!=byArchive.cend();++it){
        CResArchiveIndex archive;if(!archive.open(it.key()))return false;
        for(const auto& input:it.value())
            if(sha(archive.readMember(input["member"].toString()))!=input["sha256"].toString().toLatin1())return false;
        if(!archive.sourceIsCurrent())return false;
    }
    return true;
}
QString key(const QString& material,int reviewed,int physical,const QString& layer){return QStringLiteral("%1|%2|%3|%4").arg(material).arg(reviewed).arg(physical).arg(layer);}
QVector<QImage> tiles(const QByteArray& page){QVector<QImage> out;for(int i=0;i<64;++i){QImage image;if(!ei::decodeMmpDxt1AtlasTile(page,i,&image))return {};out.append(image);}return out;}
bool opaque(const QImage& a){for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x)if(qAlpha(a.pixel(x,y))!=255)return false;return true;}
}
bool staticMaterial(const QString& name,int reviewed,int physical){
    const auto staticClass=[](int type){return type>=0&&type<16
        &&type!=eWater&&type!=eLava&&type!=eSwamp;};
    // Water can be labelled Ground by the MP witness (e.g. a shallow patch).
    // Inspect the reviewed semantic name as well as both runtime classes.
    static const QRegularExpression water(QStringLiteral("\\b(water|lava)\\b|вода|водя|лава"),QRegularExpression::CaseInsensitiveOption);
    return !name.trimmed().isEmpty()&&staticClass(reviewed)&&staticClass(physical)
        &&!water.match(name).hasMatch();
}
bool uniformDonor(const STerrainAtlasProfileFamily& family,const STerrainAtlasProfileSlot& slot,int physical,bool animated){
    const auto& s=slot.signature;
    return slot.assigned&&!slot.ambiguous&&!family.ambiguous&&family.paintSelectable
        &&!animated&&slot.routeOwnerMaterial==0&&slot.mixedEdgeRecipes.isEmpty()
        &&s.northWest==family.id&&s.northEast==family.id
        &&s.southEast==family.id&&s.southWest==family.id
        &&staticMaterial(family.name,family.tileType,physical);
}
QString defaultRoot(){const auto override=qEnvironmentVariable("EI_MAPPER_HD_VARIANT_CACHE");if(!override.isEmpty())return override;
    return QDir(TerrainHdLibrary::root()).filePath("variants");}
Config configured(const QStringList& resources){Config c;c.cacheRoot=defaultRoot();c.mapWitnessFile=QDir(TerrainHdLibrary::root()).filePath("map-witnesses.json");QStringList roots;
    for(const auto& path:resources){QFileInfo file(path);QDir res=file.dir();
        if(file.fileName().compare("HDPack_Terrain.res",Qt::CaseInsensitive)==0)c.archives.append(file.absoluteFilePath());
        for(const auto& rel:QStringList{"../Maps","../../../Base/Maps","../Base/Maps"})if(QFileInfo(res.filePath(rel)).isDir())roots.append(QDir(res.filePath(rel)).absolutePath());
        QDir parent=res;parent.cdUp();
        if(parent.dirName().compare("Dejavu",Qt::CaseInsensitive)==0){
            const auto jmv=res.filePath("../../JamaisVu135/res/HDPack_Terrain.res");if(QFileInfo::exists(jmv))c.archives.append(QFileInfo(jmv).absoluteFilePath());}
    }
    c.archives.removeDuplicates();roots.removeDuplicates();
    if(!QFileInfo::exists(c.mapWitnessFile))for(const auto& root:roots){QDirIterator it(root,{"*.mpr"},QDir::Files,QDirIterator::Subdirectories);while(it.hasNext())c.maps.append(it.next());}
    c.maps.removeDuplicates();std::sort(c.maps.begin(),c.maps.end());return c;
}
QSet<QString> rejects(){QSet<QString> result;const auto values=QJsonDocument::fromJson(read(":/terrain_variant_rejects.json")).object()["rejects"].toArray();
    for(const auto& v:values)result.insert(v.toObject()["id"].toString());return result;}
Bank generate(const Config& config,CTerrainHdProgress* progress){
    Bank bank;QElapsedTimer timer;timer.start();auto fail=[&](const QString& error){bank.error=error;bank.cancelled=progress&&progress->cancelled();return bank;};
    auto step=[&](CTerrainHdProgress::Stage stage,int done,int total){return !progress||progress->update(stage,done,total);};
    if(config.archives.isEmpty()||(config.maps.isEmpty()&&!QFileInfo::exists(config.mapWitnessFile)))return fail(QStringLiteral("Нужны настроенные HDPack и карты с MP-классами (Base/Maps)."));
    if(!step(CTerrainHdProgress::Opening,0,config.archives.size()))return fail("Cancelled");
    QMap<QString,QJsonObject> usedInputs;
    const auto rememberInput=[&](const QString& path,const QString& member,const QByteArray& payload){
        usedInputs[path+QChar(0)+member]=QJsonObject{{"path",QFileInfo(path).absoluteFilePath()},
            {"member",member},{"sha256",QString::fromLatin1(sha(payload))}};
    };
    auto presetBytes=read(":/terrain_atlas_presets.json");const auto presets=QJsonDocument::fromJson(presetBytes).object()["presets"].toArray();
    QMap<QString,SMprMapInfo> maps,dejavuMaps;
    const bool storedWitnesses=!config.mapWitnessFile.isEmpty()&&QFileInfo::exists(config.mapWitnessFile);
    if(storedWitnesses) {
        const auto document=QJsonDocument::fromJson(read(config.mapWitnessFile)).object();
        if(document["schema"].toInt()!=1)return fail("Invalid local map witnesses");
        for(const auto& value:document["maps"].toArray()) {
            const auto object=value.toObject();SMprMapInfo info;info.innerNamespace=object["namespace"].toString();
            for(const auto& type:object["types"].toArray())info.tileTypes.append(type.toInt(-1));
            for(const auto& animation:object["animations"].toArray()){const auto a=animation.toArray();if(a.size()!=2)return fail("Invalid animation witness");SMprAnimatedTileRecord record;record.tileIndex=quint16(a[0].toInt());record.frameCount=quint16(a[1].toInt());info.animatedTiles.append(record);}
            if(info.innerNamespace.isEmpty()||info.tileTypes.isEmpty())return fail("Invalid material witness");
            (object["dejavu"].toBool()?dejavuMaps:maps)[info.innerNamespace]=info;
        }
        if(maps.isEmpty())return fail("Empty local map witnesses");
    }
    if(!storedWitnesses)for(const auto& path:config.maps){CMprDocument doc;if(!doc.load(QFileInfo(path)))return fail("Cannot read MP metadata: "+path);
        const auto info=doc.mapInfo();const auto ns=info.innerNamespace.toLower();
        // Prefer the unchanged stock/Base witness for stock slots; a published
        // map has another namespace and can never supply its predecessor MP.
        if(!maps.contains(ns)||QDir::fromNativeSeparators(path).contains("/Base/",Qt::CaseInsensitive))maps[ns]=info;
        if(QDir::fromNativeSeparators(path).contains("/Dejavu/",Qt::CaseInsensitive))dejavuMaps[ns]=info;}
    if(!storedWitnesses&&!config.mapWitnessFile.isEmpty()) {
        QJsonArray witnesses;
        auto append=[&](const QMap<QString,SMprMapInfo>& source,bool dejavu){for(auto it=source.cbegin();it!=source.cend();++it){QJsonArray types,animations;for(int type:it->tileTypes)types.append(type);for(const auto& a:it->animatedTiles)animations.append(QJsonArray{int(a.tileIndex),int(a.frameCount)});witnesses.append(QJsonObject{{"namespace",it.key()},{"dejavu",dejavu},{"types",types},{"animations",animations}});}};
        append(maps,false);append(dejavuMaps,true);
        if(progress&&progress->cancelled())return fail("Cancelled");
        if(!QDir().mkpath(QFileInfo(config.mapWitnessFile).absolutePath())||!save(config.mapWitnessFile,QJsonDocument(QJsonObject{{"schema",1},{"maps",witnesses}}).toJson()))return fail("Cannot save local map witnesses");
    }
    struct Group {QString material,layer;int reviewed{-1},physical{-1};QVector<TerrainHdVariants::Source> sources;QSet<QByteArray> hashes;};
    QMap<QString,Group> groups;int work=0;QJsonArray omissions;
    for(const auto& path:config.archives){CResArchiveIndex archive;QString error;if(!archive.open(path,&error))return fail(error);
        for(const auto& p:presets){if(!step(CTerrainHdProgress::Indexing,work++,presets.size()*config.archives.size()))return fail("Cancelled");
            const auto preset=p.toObject();QString ns=preset["namespace"].toString().toLower();int count=preset["slotCount"].toInt();
            QVector<QImage> images;QVector<QByteArray> pages;
            for(int page=0;page<256;++page){auto payload=archive.readMember(QStringLiteral("%1%2.mmp").arg(ns).arg(page,3,10,QLatin1Char('0')));if(payload.isEmpty())break;
                auto decoded=tiles(payload);if(decoded.size()!=64)break;pages.append(payload);images+=decoded;}
            if(images.size()<count)continue;
            const auto digest=CTerrainAtlasProfileInference::pixelSha256(images.mid(0,count));
            auto profile=CTerrainAtlasPresetCatalog::exactProfile(ns,digest,count);
            const auto fullDigest=CTerrainAtlasProfileInference::pixelSha256(images);
            int exact=0;QString profileMember;QByteArray profilePayload;
            for(const auto& member:archive.members())if(member.name.startsWith("ei_atlas_profile_")&&member.name.endsWith(".json")) {
                STerrainAtlasProfile candidate;
                if(STerrainAtlasProfile::fromBytes(archive.readMember(member.name),&candidate)&&candidate.approved
                    &&candidate.mprNamespace.compare(ns,Qt::CaseInsensitive)==0&&candidate.atlasSlotCount==images.size()
                    &&candidate.atlasPixelSha256==fullDigest){profile=candidate;++exact;profileMember=member.name;profilePayload=archive.readMember(member.name);}
            }
            if(exact>1)return fail("Ambiguous exact profiles: "+ns);
            if(exact)count=images.size();
            if(!profile.approved||!maps.contains(ns)){omissions.append(ns+": no exact stock/MP binding");continue;}
            const auto mp=path.contains("dejavu",Qt::CaseInsensitive)&&dejavuMaps.contains(ns)?dejavuMaps[ns]:maps[ns];
            QSet<int> animations;for(const auto& a:mp.animatedTiles)for(int f=0;f<qMax(1,int(a.frameCount));++f)animations.insert(a.tileIndex+f);
            QMap<int,STerrainAtlasProfileFamily> families;for(const auto& f:profile.families)families[f.id]=f;
            for(const auto& slot:profile.slotDefinitions){int s=slot.slot;const auto sig=slot.signature;
                if(!slot.assigned||slot.ambiguous||sig.northWest!=sig.northEast||sig.northWest!=sig.southEast||sig.northWest!=sig.southWest
                   ||s<0||s>=count||s>=mp.tileTypes.size()||animations.contains(s)||images[s].size()!=QSize(256,256)||!opaque(images[s]))continue;
                auto family=families.value(sig.northWest);
                const int physical=mp.tileTypes[s];if(!uniformDonor(family,slot,physical,animations.contains(s)))continue;
                for(const auto layer:{ETerrainAtlasLayer::Land,ETerrainAtlasLayer::Overlay})if(family.allows(layer)){
                    QString layerName=layer==ETerrainAtlasLayer::Land?"land":"overlay";auto& g=groups[key(family.name,family.tileType,physical,layerName)];
                    g.material=family.name;g.reviewed=family.tileType;g.physical=physical;g.layer=layerName;
                    auto hash=TerrainHdVariants::canonicalD4(images[s]);if(g.hashes.contains(hash))continue;
                    TerrainHdVariants::Source source;source.id=ns+":"+QString::number(s)+":"+QString::fromLatin1(terrainProvenancePixelSha(images[s]));source.pixels=images[s];source.blocks=ei::terrainTileMipChain(pages[s/64],s%64);
                    if(source.blocks.size()!=7)return fail("Missing native mip levels: "+source.id);g.sources.append(source);g.hashes.insert(hash);
                    rememberInput(path,QStringLiteral("%1%2.mmp").arg(ns).arg(s/64,3,10,QLatin1Char('0')),pages[s/64]);
                    if(!profileMember.isEmpty())rememberInput(path,profileMember,profilePayload);
                }
            }
        }
    }
    // Same owner-confirmed class-3 source as version144; never infer a new MP
    // class from the reviewed material class. Hash pin prevents substitutions.
    for(const auto& rule:terrainConfirmedHdProvenance()){
        const auto base=groups.value(key(rule.material,2,2,"land"));
        for(const auto& s:base.sources)if(terrainProvenancePixelSha(s.pixels)==rule.hdPixelSha256){
            auto& g=groups[key(rule.material,rule.reviewedClass,2,"land")];g.material=rule.material;g.reviewed=rule.reviewedClass;g.physical=2;g.layer="land";
            if(!g.hashes.contains(TerrainHdVariants::canonicalD4(s.pixels))){g.sources.append(s);g.hashes.insert(TerrainHdVariants::canonicalD4(s.pixels));}break;}}
    if(groups.isEmpty())return fail("No confirmed static uniform HD groups");
    // MPRs supply class witnesses while assembling this donor set. Persist the
    // consumed class/layer/ordered donor identities, not mutable map-file hashes.
    // Stock member payloads remain the live dependencies. Publishing unrelated
    // namespaces, MPRs or changing RES offsets/timestamps does not change them.
    QJsonArray inputs,donorIdentity;QSet<QByteArray> payloadIdentities;
    for(const auto& input:usedInputs){inputs.append(input);auto stable=input;stable.remove("path");
        payloadIdentities.insert(QJsonDocument(stable).toJson(QJsonDocument::Compact));}
    QList<QByteArray> orderedPayloads=payloadIdentities.values();std::sort(orderedPayloads.begin(),orderedPayloads.end());
    QByteArray cacheIdentity=QByteArray::number(TerrainHdVariants::AlgorithmVersion)+sha(presetBytes);
    for(const auto& value:orderedPayloads)cacheIdentity+=value+'\n';
    for(auto it=groups.cbegin();it!=groups.cend();++it){QJsonArray donors;
        for(const auto& s:it->sources)donors.append(s.id);
        donorIdentity.append(QJsonObject{{"key",it.key()},{"sources",donors}});}
    cacheIdentity+=QJsonDocument(donorIdentity).toJson(QJsonDocument::Compact);
    const auto bankId=sha(cacheIdentity);bank.directory=QDir(config.cacheRoot).filePath(QString::fromLatin1(bankId));
    QJsonArray metadata,groupMetadata;QVector<TerrainHdVariants::Variant> variants;int rejected=0,shortfalls=0,index=0;
    for(auto it=groups.cbegin();it!=groups.cend();++it,++index){
        if(!step(CTerrainHdProgress::Recovering,index,groups.size()))return fail("Cancelled");const auto& group=it.value();
        auto result=TerrainHdVariants::generate(it.key(),group.sources,[&](int,int){return !progress||!progress->cancelled();});
        if(!result.success)return fail(result.error);rejected+=result.rejected;if(result.shortfall)++shortfalls;
        QJsonArray originals;for(const auto& s:group.sources)originals.append(s.id);
        groupMetadata.append(QJsonObject{{"key",it.key()},{"material",group.material},{"reviewedClass",group.reviewed},{"physicalClass",group.physical},{"layer",group.layer},
            {"originals",originals},{"generated",result.variants.size()},{"shortfall",result.shortfall},{"shortfallReason",result.shortfallReason},{"rejected",result.rejected},{"rejections",result.rejections}});
        for(auto v:result.variants){auto m=v.metadata;m["material"]=group.material;m["reviewedClass"]=group.reviewed;m["physicalClass"]=group.physical;m["layer"]=group.layer;m["slot"]=variants.size();metadata.append(m);variants.append(v);}
    }
    bank.manifest={{"algorithm",TerrainHdVariants::AlgorithmVersion},{"id",QString::fromLatin1(bankId)},{"inputs",inputs},{"presetSha",QString::fromLatin1(sha(presetBytes))},
        {"groups",groupMetadata},{"entries",metadata},{"generated",variants.size()},{"rejected",rejected},{"shortfalls",shortfalls},{"omissions",omissions}};
    if(!current(bank.manifest))return fail("Sources changed during generation");
    if(!step(CTerrainHdProgress::Preparing,0,(variants.size()+63)/64))return fail("Cancelled");
    const QString stage=bank.directory+".preparing";if(QFileInfo::exists(stage))return fail("Previous interrupted generation exists: "+stage);
    if(!QDir().mkpath(stage))return fail("Cannot create user cache");
    // Only this new staging directory is owned by this call.
    auto cleanup=[&](){QDir(stage).removeRecursively();};QJsonArray pageMetadata;
    for(int page=0;page<(variants.size()+63)/64;++page){if(!step(CTerrainHdProgress::Preparing,page,(variants.size()+63)/64)){cleanup();return fail("Cancelled");}
        QImage pixels(2048,2048,QImage::Format_ARGB32);pixels.fill(Qt::black);
        for(int local=0;local<64&&page*64+local<variants.size();++local){const auto& v=variants[page*64+local];
            for(int y=0;y<256;++y)std::memcpy(pixels.scanLine((7-local/8)*256+y)+(local%8)*1024,v.pixels.constScanLine(y),1024);}
        QString error;auto payload=ei::encodeMmpDxt1(pixels,true,&error);if(payload.isEmpty()){cleanup();return fail(error);}
        for(int local=0;local<64&&page*64+local<variants.size();++local)if(!ei::transplantTerrainMipChain(&payload,local,variants[page*64+local].blocks)){cleanup();return fail("BC1 page transplant failed");}
        const QString name=QStringLiteral("variants%1.mmp").arg(page,3,10,QLatin1Char('0'));if(!save(stage+"/"+name,payload)){cleanup();return fail("Cannot save cache page");}
        pageMetadata.append(QJsonObject{{"file",name},{"sha256",QString::fromLatin1(sha(payload))},{"bytes",payload.size()}});
    }
    bank.manifest["pages"]=pageMetadata;
    localInputs(&bank.manifest,QFileInfo(config.cacheRoot).absolutePath());
    // Timing is a receipt, excluded from deterministic bank bytes.
    if(!save(stage+"/manifest.json",QJsonDocument(bank.manifest).toJson())){cleanup();return fail("Cannot save manifest");}
    if(progress&&progress->cancelled()){cleanup();return fail("Cancelled");}
    if(QFileInfo::exists(bank.directory)){
        bool same=read(stage+"/manifest.json")==read(bank.directory+"/manifest.json");for(const auto& v:pageMetadata){const auto name=v.toObject()["file"].toString();same= same&&read(stage+"/"+name)==read(bank.directory+"/"+name);}
        cleanup();if(!same)return fail("Deterministic cache collision: existing bank differs");
    }else if(!QDir().rename(stage,bank.directory)){cleanup();return fail("Cannot publish cache generation");}
    if(!save(QDir(config.cacheRoot).filePath("current.json"),QJsonDocument(QJsonObject{{"id",QString::fromLatin1(bankId)}}).toJson()))return fail("Cannot publish bank pointer");
    bank=load(config.cacheRoot);bank.manifest["elapsedMs"]=double(timer.elapsed());step(CTerrainHdProgress::Complete,1,1);return bank;
}
Bank load(const QString& root,QString* error){Bank bank;auto fail=[&](const QString& reason){bank.error=reason;if(error)*error=reason;return bank;};
    QString id=QJsonDocument::fromJson(read(QDir(root).filePath("current.json"))).object()["id"].toString();
    if(id.size()!=64||id.contains('/')||id.contains('\\')||id.contains('.'))return fail("No generated bank");bank.directory=QDir(root).filePath(id);
    bank.manifest=QJsonDocument::fromJson(read(bank.directory+"/manifest.json")).object();
    const auto libraryRoot=QFileInfo(root).absolutePath();
    if(bank.manifest["algorithm"].toInt()!=TerrainHdVariants::AlgorithmVersion||bank.manifest["presetSha"].toString().toLatin1()!=sha(read(":/terrain_atlas_presets.json"))||!current(bank.manifest,libraryRoot))return fail("Generated bank sources changed; regenerate");
    QVector<QByteArray> pages;for(const auto& v:bank.manifest["pages"].toArray()){auto object=v.toObject();QString name=object["file"].toString();if(name.contains('/')||name.contains('\\')||name.contains(".."))return fail("Unsafe bank page");auto payload=read(bank.directory+"/"+name);
        if(sha(payload)!=object["sha256"].toString().toLatin1())return fail("Damaged bank page");pages.append(payload);}
    for(const auto& v:bank.manifest["entries"].toArray()){auto object=v.toObject();Entry e;e.metadata=object;e.id=object["id"].toString();e.material=object["material"].toString();e.reviewedClass=object["reviewedClass"].toInt(-1);e.physicalClass=object["physicalClass"].toInt(-1);e.layer=object["layer"].toString();e.slot=object["slot"].toInt(-1);
        if(e.slot<0||e.slot/64>=pages.size()||!staticMaterial(e.material,e.reviewedClass,e.physicalClass))return fail("Invalid bank identity");
        e.blocks=ei::terrainTileMipChain(pages[e.slot/64],e.slot%64);if(!ei::decodeMmpDxt1AtlasTile(pages[e.slot/64],e.slot%64,&e.pixels)||!ei::rememberTerrainMipChain(e.pixels,e.blocks))return fail("Invalid protected mip chain");bank.entries.append(e);}
    for(const auto& input:bank.manifest["inputs"].toArray()) {
        const auto path=inputPath(input.toObject()["path"].toString(),libraryRoot);QFileInfo info(path);
        bank.sourceStats[path]=qMakePair(info.size(),info.lastModified().toMSecsSinceEpoch());
    }
    bank.success=true;return bank;
}
bool migrateLegacy(const QStringList& localArchives,QString* error) {
    const QString destination=defaultRoot();
    if(QFileInfo::exists(QDir(destination).filePath("current.json")))return true;
    const QString legacy=QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("EIMapperAtaman/terrain-variants");
    const QString id=QJsonDocument::fromJson(read(legacy+"/current.json")).object()["id"].toString();
    if(!QRegularExpression("^[0-9a-f]{64}$").match(id).hasMatch())return true; // no previous bank
    const QString oldDirectory=legacy+"/"+id;
    auto manifest=QJsonDocument::fromJson(read(oldDirectory+"/manifest.json")).object();
    if(manifest["algorithm"].toInt()!=TerrainHdVariants::AlgorithmVersion||manifest["presetSha"].toString().toLatin1()!=sha(read(":/terrain_atlas_presets.json")))return true;
    auto fail=[&](const QString& text){if(error)*error=text;return false;};
    QVector<QSharedPointer<CResArchiveIndex>> archives;
    for(const auto& path:localArchives){QSharedPointer<CResArchiveIndex> archive(new CResArchiveIndex);if(!archive->open(path,error))return false;archives.append(archive);}
    QJsonArray inputs;
    for(const auto& value:manifest["inputs"].toArray()) {
        auto input=value.toObject();bool found=false;
        for(int i=0;i<archives.size();++i)if(sha(archives[i]->readMember(input["member"].toString()))==input["sha256"].toString().toLatin1()) {
            input["path"]=localArchives[i];inputs.append(input);found=true;break;
        }
        if(!found)return true; // incompatible old bank; explicit generation can replace it
    }
    if(inputs.isEmpty())return true;
    manifest["inputs"]=inputs;localInputs(&manifest,QFileInfo(destination).absolutePath());manifest.remove("id");
    const auto newId=QString::fromLatin1(sha(QJsonDocument(manifest).toJson(QJsonDocument::Compact)));manifest["id"]=newId;
    if(!QDir().mkpath(destination))return fail("Cannot create local variant bank");
    QTemporaryDir stage(destination+"/migrate-XXXXXX");if(!stage.isValid())return fail("Cannot stage variant bank");
    for(const auto& value:manifest["pages"].toArray()) {
        const auto page=value.toObject();const auto name=page["file"].toString();
        if(name.contains('/')||name.contains('\\')||name.contains(".."))return fail("Unsafe legacy bank page");
        const auto payload=read(oldDirectory+"/"+name);
        if(sha(payload)!=page["sha256"].toString().toLatin1()||!save(stage.filePath(name),payload))return fail("Cannot migrate legacy bank page");
    }
    if(!save(stage.filePath("manifest.json"),QJsonDocument(manifest).toJson()))return fail("Cannot migrate bank manifest");
    const QString final=destination+"/"+newId;
    if(!QFileInfo::exists(final)){if(!QDir().rename(stage.path(),final))return fail("Cannot publish migrated bank");stage.setAutoRemove(false);}
    if(!save(destination+"/current.json",QJsonDocument(QJsonObject{{"id",newId}}).toJson()))return fail("Cannot select migrated bank");
    return load(destination,error).success;
}
QSharedPointer<const Bank> available(){QMutexLocker lock(&mutex);const auto root=defaultRoot();
    if(!cached||cachedRoot!=root){cached.reset(new Bank(load(root)));cachedRoot=root;}
    if(!cached->success)return {};
    for(auto it=cached->sourceStats.cbegin();it!=cached->sourceStats.cend();++it) {
        QFileInfo info(it.key());
        if(!info.exists()||qMakePair(info.size(),info.lastModified().toMSecsSinceEpoch())!=it.value()){
            // A republished RES needs a new index, not a new generated bank.
            cached.reset(new Bank(load(root)));if(!cached->success)return {};return cached;
        }
    }
    return cached;
}
void forget(){QMutexLocker lock(&mutex);cached.clear();}
QStringList groupNames(const Bank& bank){QStringList names;for(const auto& v:bank.manifest["groups"].toArray())names.append(v.toObject()["key"].toString());return names;}
QVector<Entry> forMaterial(const Bank& bank,const QString& name,int reviewed,ETerrainAtlasLayer layer,const QSet<QString>& excluded){QVector<Entry> result;
    for(const auto& e:bank.entries)if(staticMaterial(e.material,e.reviewedClass,e.physicalClass)&&!excluded.contains(e.id)&&e.reviewedClass==reviewed&&e.layer==(layer==ETerrainAtlasLayer::Land?"land":"overlay")
        &&(e.material==name||CTerrainAtlasPresetCatalog::reviewedMaterialName(e.material,e.reviewedClass)==name))result.append(e);return result;}
bool import(CTerrainWorkingAtlas* atlas,const QVector<Entry>& entries,qint32 material,CValidationReport* report){
    if(!atlas||entries.isEmpty())return true;STerrainTransitionSignature s;s.northWest=s.northEast=s.southEast=s.southWest=material;
    QSet<int> classes;for(const auto& v:atlas->catalog().paintVariants(s))classes.insert(atlas->runtimeTerrainTypeForSlot(SMprPackedTile::decode(v.packedTile).atlasSlot()));
    for(const auto& e:entries) {
        if(!staticMaterial(e.material,e.reviewedClass,e.physicalClass))continue;
        int targetClass=classes.contains(e.physicalClass)?e.physicalClass:-1;
        // Version144 explicitly binds the class-3 Stone/Desert material to
        // the pinned native HD witness whose source MP class is 2. Preserve
        // the target anchor's class exactly, as its ordinary palette tile does.
        if(targetClass<0&&classes.contains(e.reviewedClass))
            for(const auto& rule:terrainConfirmedHdProvenance())
                if(e.material==rule.material&&e.reviewedClass==rule.reviewedClass
                    &&e.metadata["anchor"].toString().endsWith(QString::fromLatin1(rule.hdPixelSha256)))targetClass=e.reviewedClass;
        if(targetClass>=0&&atlas->importPhysicalTile(e.pixels,targetClass,&s,QStringLiteral("hd-variant:")+e.id,e.slot,report,true)<0)return false;
    }
    return true;
}
int runCheck(const QString& path){auto object=QJsonDocument::fromJson(read(path)).object();Config c;for(const auto& v:object["archives"].toArray())c.archives.append(v.toString());for(const auto& v:object["maps"].toArray())c.maps.append(v.toString());c.cacheRoot=object["cacheRoot"].toString();
    const auto root=QFileInfo(path).absoluteDir().absolutePath();if(!QFileInfo(root).fileName().startsWith("build-tests-")||!QFileInfo(c.cacheRoot).absoluteFilePath().startsWith(root+"/"))return 2;
    CTerrainHdProgress p;QTextStream out(stdout);
    if(object["importLibrary"].toBool()) {
        const QString library=QFileInfo(c.cacheRoot).absolutePath();
        if(!library.startsWith(root+"/"))return 2;
        qputenv("EI_MAPPER_HD_LIBRARY",library.toUtf8());qputenv("EI_MAPPER_HD_VARIANT_CACHE",c.cacheRoot.toUtf8());
        QString error;QStringList local;QElapsedTimer timer;timer.start();
        if(!TerrainHdLibrary::ensure(c.archives,&local,&error,&p,library)){out<<error<<endl;return 1;}
        CTerrainHdSources sources;
        if(!sources.open(local,&error,&p,library+"/index")||!sources.saveCache()||!migrateLegacy(local,&error)){out<<error<<endl;return 1;}
        out<<"Local library: "<<local.size()<<" archives; "<<sources.sourceCount()<<" HD tiles; cacheHit="<<sources.cacheHit()<<"; elapsedMs="<<timer.elapsed()<<endl;
        if(object["libraryOnly"].toBool())return 0;
        c.archives=local;c.mapWitnessFile=library+"/map-witnesses.json";
    }
    auto bank=generate(c,&p);out<<QJsonDocument(bank.manifest).toJson(QJsonDocument::Compact)<<endl<<bank.error<<endl;return bank.success?0:1;}
}
