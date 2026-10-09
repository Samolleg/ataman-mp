// Standalone read-only research exporter. Not linked into the mapper.
// Uses the project's exact MPR, MMP and semantic catalog implementations.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCodec>
#include <QTextStream>
#include "endurance_document.h"
#include "mpr_document.h"
#include "mmp_inspector.h"
#include "terrain_atlas_profile.h"
#include "terrain_virtual_atlas.h"
#include "resource_name.h"
#include "terrain_semantic_field_store.h"

struct Entry { qint64 offset; qint64 size; };
struct Archive { QString path; QMap<QString, Entry> entries; };
static Archive indexArchive(const QString& path) {
    Archive a; a.path=path; QFile f(path);
    if(!f.open(QIODevice::ReadOnly)) return a;
    QDataStream s(&f); s.setByteOrder(QDataStream::LittleEndian);
    quint32 magic,n,offset,namesLen; s>>magic>>n>>offset>>namesLen;
    if(magic!=0x019ce23c || n>10000000 || quint64(offset)+quint64(n)*22+namesLen>quint64(f.size())) return a;
    f.seek(offset);
    struct Row { quint32 next,size,off,time; quint16 len; quint32 name; };
    QVector<Row> rows; rows.resize(int(n));
    for(auto& r:rows) s>>r.next>>r.size>>r.off>>r.time>>r.len>>r.name;
    QByteArray names=f.read(namesLen); auto codec=QTextCodec::codecForName("CP1251");
    for(const auto& r:rows) if(r.len && quint64(r.name)+r.len<=quint64(names.size()) && quint64(r.off)+r.size<=quint64(f.size()))
        a.entries.insert(codec->toUnicode(names.mid(r.name,r.len)).toLower(), {r.off,r.size});
    return a;
}
static QByteArray member(const QVector<Archive>& archives,const QString& name,QString* source=nullptr) {
    for(const auto& a:archives) { auto i=a.entries.constFind(name.toLower()); if(i==a.entries.constEnd()) continue;
        QFile f(a.path); if(!f.open(QIODevice::ReadOnly)||!f.seek(i->offset)) continue;
        if(source) *source=a.path;
        return f.read(i->size);
    } return {};
}
static QJsonArray sig(const STerrainTransitionSignature& s) {return {s.northWest,s.northEast,s.southEast,s.southWest};}
static void writeJson(QString path,QJsonObject obj) {
    QFile f(path); if(!f.open(QIODevice::WriteOnly)||f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact))<0) qFatal("Cannot write output");
}
struct Atlas { QVector<QImage> tiles; STerrainAtlasProfile profile; STerrainAtlasAnalysis analysis; QString directory; QJsonObject manifest; bool exact{false}; QString basis; };
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); auto args=app.arguments();
    if(args.size()!=3) {qCritical("Usage: terrain_bake_export config.json output-directory");return 2;}
    QFile config(args[1]); if(!config.open(QIODevice::ReadOnly)) return 2;
    auto cfg=QJsonDocument::fromJson(config.readAll()).object();
    QDir out(QFileInfo(args[2]).absoluteFilePath()); if(out.dirName()!="build-tests-terrain-bake-research-0") return 2;
    QDir().mkpath(out.absolutePath()); QJsonArray allMaps,allAtlases;
    for(auto pv:cfg["profiles"].toArray()) {
        auto p=pv.toObject(); QString profile=p["id"].toString(); QVector<Archive> archives;
        QVector<QString> roots;
        for(auto rv:p["resourceRoots"].toArray()) roots.append(rv.toString());
        for(auto path:ei::orderedResourceSources(roots)) archives.append(indexArchive(path));
        QMap<QString,Atlas> cache;
        for(auto mv:p["mapRoots"].toArray()) {
            QDir root(mv.toString());
            for(auto file:root.entryList({"*.mpr"},QDir::Files,QDir::Name)) {
                QString path=root.absoluteFilePath(file); CValidationReport report; CMprDocument doc;
                QJsonObject row{{"profile",profile},{"path",path},{"file",file}};
                if(!doc.load(QFileInfo(path),&report)) {row["error"]=report.toText();allMaps.append(row);continue;}
                auto info=doc.mapInfo(); QString ns=info.innerNamespace; int fullSlots=int(info.textureCount)*64; QString key=ns.toLower()+":"+QString::number(fullSlots);
                if(!cache.contains(key)) {
                    Atlas a; a.directory="atlases/"+profile+"-"+ns.toLower(); QDir().mkpath(out.filePath(a.directory));
                    QJsonArray pages; bool complete=true;
                    for(uint page=0;page<info.textureCount;page++) {
                        QString name=QString("%1%2.mmp").arg(ns.toLower()).arg(page,3,10,QChar('0')); QString source;
                        QByteArray payload=member(archives,name,&source); auto inspected=ei::inspectMmp(payload);
                        if(!inspected.valid) {complete=false;break;}
                        QImage decoded=inspected.baseImage.mirrored(false,true); int size=decoded.width()/8;
                        pages.append(QJsonObject{{"name",name},{"source",source},{"sha256",QString(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex())},{"width",decoded.width()}});
                        for(int slot=0;slot<64;slot++)
                            a.tiles.append(decoded.copy((slot%8)*size,(slot/8)*size,size,size).mirrored(false,true).convertToFormat(QImage::Format_RGBA8888));
                    }
                    QByteArray hash=CTerrainAtlasProfileInference::pixelSha256(a.tiles);
                    if(complete && a.tiles.size()==fullSlots) {
                        a.profile=CTerrainAtlasPresetCatalog::exactProfile(ns,hash,fullSlots);
                        a.basis="exact-preset";
                        STerrainAtlasProfile embedded;
                        QByteArray eb=doc.memberPayload(CTerrainAtlasProfileStore::embeddedMemberName(ns));
                        if(eb.isEmpty()) eb=member(archives,CTerrainAtlasProfileStore::embeddedMemberName(ns));
                        if(!eb.isEmpty() && STerrainAtlasProfile::fromBytes(eb,&embedded)
                           && embedded.validateBinding(ns,hash,fullSlots)) {a.profile=embedded;a.basis="exact-embedded";}
                        // Exact authored overrides take precedence, as in the mapper.
                        STerrainAtlasProfile local; QString localPath;
                        if(CTerrainAtlasProfileStore::loadExplicitOverride(ns,hash,fullSlots,&local,cfg["overrideRoot"].toString(),&localPath)) {a.profile=local;a.basis="exact-override";}
                        a.exact=a.profile.approved;
                        if(!a.exact) {
                            // Research-only conditional topology. Never relabel this
                            // namespace/slot-count match as an exact pixel binding.
                            QFile presets(":/terrain_atlas_presets.json");presets.open(QIODevice::ReadOnly);
                            for(auto v:QJsonDocument::fromJson(presets.readAll()).object()["presets"].toArray()) {
                                auto preset=v.toObject();
                                if(preset["namespace"].toString().compare(ns,Qt::CaseInsensitive)==0 && preset["slotCount"].toInt()==fullSlots) {
                                    QByteArray accepted=preset["bindings"].toArray().first().toObject()["pixelSha256"].toString().toLatin1();
                                    a.profile=CTerrainAtlasPresetCatalog::exactProfile(ns,accepted,fullSlots);a.basis="HYPOTHESIS-namespace-slot-layout";break;
                                }
                            }
                        }
                        a.analysis=a.profile.approvedAnalysis();
                        for(int slot=0;slot<a.tiles.size();slot++) if(!a.tiles[slot].save(out.filePath(a.directory+QString("/%1.png").arg(slot)))) return 3;
                    }
                    a.manifest=QJsonObject{{"key",key},{"profile",profile},{"namespace",ns},{"directory",a.directory},{"pixelSha256",QString(hash)},{"complete",complete},{"semanticExact",a.exact},{"semanticBasis",a.basis},{"pages",pages},{"catalog",a.profile.toJson()}};
                    allAtlases.append(a.manifest);cache.insert(key,a);
                }
                const Atlas& a=cache[key]; CTerrainEditModel model;
                if(!model.load(doc,&report)) {row["error"]=report.toText();allMaps.append(row);continue;}
                row["sha256"]=QString(QCryptographicHash::hash(doc.originalBytes(),QCryptographicHash::Sha256).toHex());
                row["namespace"]=ns;row["atlasDirectory"]=a.directory;row["semanticExact"]=a.exact;row["semanticBasis"]=a.basis;
                row["width"]=model.tileCountX();row["height"]=model.tileCountY();row["atlasSlots"]=fullSlots;row["mpTileCount"]=int(info.tileCount);row["mpMaterials"]=int(info.materialCount);
                QJsonArray land,overlay,landSigs,overlaySigs,classes;
                for(auto c:info.tileTypes) classes.append(c);
                row["runtimeTypes"]=classes;
                for(int y=0;y<model.tileCountY();y++) for(int x=0;x<model.tileCountX();x++) {
                    bool avail=false,found=false; auto packed=model.packedTile(x,y,ETerrainTileLayer::Land,&avail);
                    land.append(avail?int(packed):-1);auto ls=a.analysis.landCatalog.gridSignatureForPacked(packed,&found);landSigs.append(avail&&found?sig(ls):QJsonArray());
                    packed=model.packedTile(x,y,ETerrainTileLayer::Overlay,&avail);
                    bool active=avail&&packed!=0xffffu&&model.overlayMaterialIndex(x,y)>=0;
                    overlay.append(active?int(packed):-1);auto os=a.analysis.overlayCompatibleCatalog.gridSignatureForPacked(packed,&found);overlaySigs.append(active&&found?sig(os):QJsonArray());
                }
                row["land"]=land;row["overlay"]=overlay;row["landSignatures"]=landSigs;row["overlaySignatures"]=overlaySigs;
                // Recover on the in-memory model only; report disagreement separately.
                CValidationReport recovery; bool restored=false;
                STerrainSemanticFieldDocument saved;
                QByteArray savedBytes=doc.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
                if(savedBytes.isEmpty()) savedBytes=member(archives,STerrainSemanticFieldDocument::memberName(ns,QFileInfo(path)));
                if(a.exact && !savedBytes.isEmpty() && STerrainSemanticFieldDocument::fromBytes(savedBytes,&saved) && !saved.land.isEmpty()
                   && saved.validateBinding(ns,QByteArray::fromHex(row["sha256"].toString().toLatin1()),QByteArray::fromHex(a.profile.atlasPixelSha256),
                        QCryptographicHash::hash(a.profile.toBytes(),QCryptographicHash::Sha256),model.tileCountX()+1,model.tileCountY()+1,nullptr,STerrainSemanticFieldDocument::tileBindingSha256(doc)))
                    restored=model.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,saved.land,saved.land,&recovery);
                bool recovered=restored || (a.analysis.usable&&model.initializeVirtualMaterialField(ETerrainTileLayer::Land,a.analysis.landCatalog,&recovery,ETerrainMaterialFieldRecovery::RecoverDeterministically));
                row["fieldBasis"]=restored?"exact-saved-field":"deterministic-corner-recovery";
                row["fieldRecovered"]=recovered;row["recoveryReport"]=recovery.toText(); QJsonArray field;
                if(recovered) for(int y=0;y<=model.tileCountY();y++) for(int x=0;x<=model.tileCountX();x++) field.append(model.virtualMaterialAt(ETerrainTileLayer::Land,x,y));
                row["field"]=field;
                QString output="maps/"+profile+"-"+file+".json"; QDir().mkpath(out.filePath("maps"));writeJson(out.filePath(output),row);
                for(auto k:{"land","overlay","landSignatures","overlaySignatures","field","runtimeTypes"}) row.remove(k);
                row["export"]=output;allMaps.append(row); QTextStream(stdout)<<profile<<" "<<file<<" "<<model.tileCountX()<<"x"<<model.tileCountY()<<" exact="<<a.exact<<" basis="<<a.basis<<"\n";
            }
        }
    }
    writeJson(out.filePath("inventory.json"),{{"maps",allMaps},{"atlases",allAtlases},{"config",cfg}});return 0;
}
