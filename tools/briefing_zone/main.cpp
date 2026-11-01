#include <QtCore>
#include <QtEndian>
#include <functional>
#include "endurance_document.h"
#include "mpr_document.h"
#include "res_archive_document.h"
#include "terrain_edit_model.h"
#include "map_table_document.h"
#include "script_reference_analyzer.h"

namespace {
QByteArray read(const QString& p) {QFile f(p);if(!f.open(QIODevice::ReadOnly))throw f.errorString();return f.readAll();}
void write(const QString& p,const QByteArray& b) {QSaveFile f(p);if(!f.open(QIODevice::WriteOnly)||f.write(b)!=b.size()||!f.commit())throw QString("write: ")+p;}
QJsonArray sections(const QVector<SMobSection>& ss) {
 QJsonArray a;for(const auto& s:ss){QJsonObject j{{"type",double(s.type)},{"path",s.path},{"offset",double(s.offset)},{"size",s.rawBytes.size()},{"sha256",QString(s.sha256.toHex())}};
 if(s.container)j["children"]=sections(s.children);else j["payload"]=QString(s.rawBytes.mid(8).toBase64());a.append(j);}return a;
}
CMobDocument mob(const QString& p){CMobDocument d;CValidationReport r;if(!d.loadBytes(read(p),p,&r)||r.hasErrors())throw r.toText();return d;}
CMprDocument mpr(const QString& p){CMprDocument d;CValidationReport r;if(!d.loadBytes(read(p),p,&r)||r.hasErrors())throw r.toText();return d;}
}
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);QTextStream err(stderr);err.setCodec("UTF-8");
 try {
 const auto a=app.arguments();if(a.size()!=4)throw QString("usage: briefing_zone inspect|validate|compose input output");
 if(a[1]=="audit") {
  const auto p=QJsonDocument::fromJson(read(a[2])).object();QJsonArray files;
  for(const auto v:p["files"].toArray()) {
   auto j=v.toObject();const auto bytes=read(j["path"].toString());
   if(j["kind"]=="text"){
    QJsonArray refs;for(const auto& r:CScriptReferenceAnalyzer::findObjectIds(QString::fromLatin1(bytes)))refs.append(QJsonObject{{"id",double(r.objectId)},{"line",r.line},{"context",r.context}});
    j["references"]=refs;j["sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());files.append(j);continue;
   }
   CMobDocument d;CValidationReport report;
   const bool ok=d.loadBytes(bytes,j["path"].toString(),&report)&&!report.hasErrors();j["strictParse"]=ok;j["validation"]=report.toJson();
   j["sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
   if(ok){QJsonArray ids,parents,refs;for(const auto& o:d.objects())if(o.hasNid)ids.append(double(o.nid));for(const auto& o:d.auxiliaryIds())ids.append(double(o.id));j["objectIds"]=ids;
    std::function<void(const QVector<SMobSection>&)> walk=[&](const QVector<SMobSection>& ss){for(const auto& s:ss){if(s.type==0xb012&&s.rawBytes.size()==12)parents.append(QJsonObject{{"path",s.path},{"id",double(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(s.rawBytes.constData()+8)))}});walk(s.children);}};
    walk(d.sections());j["parentReferences"]=parents;
    if(d.hasSection(0xacceeccb)){QString script;CValidationReport sr;if(d.embeddedScript(&script,&sr)){
     QByteArray raw;QString error;if(!CMobDocument::encodeCp1251Strict(script,&raw,&error))throw error;j["scriptBytes"]=QString::fromLatin1(raw.toBase64());
     for(const auto& r:CScriptReferenceAnalyzer::findObjectIds(script))refs.append(QJsonObject{{"id",double(r.objectId)},{"line",r.line},{"context",r.context}});
    }else j["scriptError"]=sr.toText();}j["references"]=refs;
   }files.append(j);
  }write(a[3],QJsonDocument(QJsonObject{{"files",files}}).toJson());
 } else if(a[1]=="catalog") {
  const auto p=QJsonDocument::fromJson(read(a[2])).object();const auto bytes=read(p["archive"].toString()),map=read(p["map"].toString());
  CResArchiveDocument archive;QStringList errors;if(!archive.loadBytes(bytes,"copied-texts.res",&errors))throw errors.join("\n");
  CMapTableDocument table;if(!table.loadBytes(map,&errors)||table.serialize()!=map)throw errors.join("\n");
  const SMapTableZone *base=nullptr,*zone=nullptr;for(const auto& z:table.zones()){if(z.zoneId=="bz1g")base=&z;if(z.zoneId=="gz1g")zone=&z;}
  CMapTableDocument original;if(!original.loadBytes(archive.memberPayload("map.txt"),&errors))throw errors.join("\n");const SMapTableZone* oldBase=nullptr;
  for(const auto& z:original.zones())if(z.zoneId=="bz1g")oldBase=&z;
  if(!base||!zone||!oldBase||base->exits.isEmpty()||oldBase->exits.isEmpty()||zone->exits.size()<2
     ||base->exits[0].destinationZoneId!=oldBase->exits[0].destinationZoneId||base->exits[0].destinationExitId!=oldBase->exits[0].destinationExitId
     ||zone->exits[1].destinationZoneId!="bz1g"||zone->exits[1].destinationExitId!=1)throw QString("Original travel target was changed");
  if(p["mapOnly"].toBool()){
   const auto& target=base->exits[0];QJsonObject result{{"validated",true},{"archiveWritten",false},{"destination",target.destinationZoneId},{"exit",target.destinationExitId}};
   for(const auto& edge:table.zones())if(edge.zoneId==target.destinationZoneId&&target.destinationExitId>0&&target.destinationExitId<=edge.exits.size()){
    const auto& next=edge.exits[target.destinationExitId-1];result["destinationZoneType"]=edge.zoneType;result["nextZone"]=next.destinationZoneId;result["nextExit"]=next.destinationExitId;
   }write(a[3],QJsonDocument(result).toJson());return 0;
  }
  const int index=archive.uniqueMemberIndex("map.txt");if(index<0)throw QString("Missing unique map.txt");
  QByteArray candidate;const auto hash=archive.members()[index].sha256;
  if(!archive.replaceMember("map.txt",hash,map,&candidate,&errors))throw errors.join("\n");
  CResArchiveDocument reopened;if(!reopened.loadBytes(candidate,"candidate-texts.res",&errors)||!CResArchiveDocument::validateReplacement(archive,reopened,"map.txt",map,&errors))throw errors.join("\n");
  write(a[3],candidate);
 } else if(a[1]=="sample") {
  const auto p=QJsonDocument::fromJson(read(a[2])).object();auto d=mpr(p["mpr"].toString());CTerrainEditModel terrain;CValidationReport r;
  if(!terrain.load(d,&r)||r.hasErrors())throw r.toText();QJsonArray samples;
  for(const auto v:p["points"].toArray()){auto q=v.toObject();bool available=false;const float x=float(q["x"].toDouble()),y=float(q["y"].toDouble());
   q["terrainZ"]=terrain.terrainSurfaceHeight(x,y,&available);q["available"]=available;samples.append(q);}
  write(a[3],QJsonDocument(samples).toJson());
 } else if(a[1]=="inspect"||a[1]=="validate") {
  QJsonObject j;const auto bytes=read(a[2]);CValidationReport r;
  if(a[2].endsWith(".mob",Qt::CaseInsensitive)) {
   CMobDocument d;if(!d.loadBytes(bytes,a[2],&r)||r.hasErrors()||d.serialize()!=bytes)throw r.toText();
   j["sections"]=sections(d.sections());QJsonArray oo;for(const auto& o:d.objects())oo.append(o.toJson());j["objects"]=oo;
   QString script;if(!d.embeddedScript(&script,&r))throw r.toText();j["script"]=script;
   QByteArray encoded;QString encodingError;CMobDocument::encodeCp1251Strict(script,&encoded,&encodingError);j["scriptCp1251"]=QString::fromLatin1(encoded.toBase64());
   QByteArray scriptNoOp;j["scriptNoOpByteIdentical"]=d.replaceEmbeddedScript(script,&scriptNoOp,&r)&&scriptNoOp==bytes;
  } else if(a[2].endsWith(".mpr",Qt::CaseInsensitive)) {
   CMprDocument d;if(!d.loadBytes(bytes,a[2],&r)||r.hasErrors()||d.serialize()!=bytes)throw r.toText();
   const auto& m=d.mapInfo();j["namespace"]=m.innerNamespace;j["sectorsX"]=int(m.sectorCountX);j["sectorsY"]=int(m.sectorCountY);j["maxZ"]=m.maxZ;
   QJsonArray mm;for(const auto& s:d.members())mm.append(QJsonObject{{"name",s.name},{"size",s.payload.size()},{"payload",QString(s.payload.toBase64())}});j["members"]=mm;
  } else {
   CResArchiveDocument d;QStringList errors;if(!d.loadBytes(bytes,a[2],&errors)||d.serialize()!=bytes)throw errors.join("\n");
   QJsonArray names;for(const auto& s:d.members())names.append(s.name);j["members"]=names;j["map.txt"]=QString::fromLocal8Bit(d.memberPayload("map.txt"));
   // map.txt is CP1251, independent of the process locale.
   j["map.txt"]=QTextCodec::codecForName("Windows-1251")->toUnicode(d.memberPayload("map.txt"));
   j["mapPayload"]=QString::fromLatin1(d.memberPayload("map.txt").toBase64());
  }
  j["validation"]=r.toJson();j["noOpByteIdentical"]=true;j["sha256"]=QString(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());write(a[3],QJsonDocument(j).toJson());
 } else if(a[1]=="compose") {
  const auto p=QJsonDocument::fromJson(read(a[2])).object();
  auto base=mob(p["baseMob"].toString());auto zone=mob(p["zoneMob"].toString());CValidationReport r;QByteArray bytes;
  if(p.contains("zoneLeaves")){
   CMobEditTransaction ztx("Explicit static ID corrections");for(const auto e:p["zoneLeaves"].toArray()){const auto o=e.toObject();ztx.replaceLeafPayload(o["path"].toString(),QByteArray::fromHex(o["sha256"].toString().toLatin1()),QByteArray::fromBase64(o["payload"].toString().toLatin1()));}
   if(!zone.applyTransaction(ztx,&bytes,&r)||!zone.loadBytes(bytes,"renumbered-zone1-copy.mob",&r))throw r.toText();
  }
  CMobEditTransaction tx("briefing coordinate migration");
  for(const auto e:p["leaves"].toArray()){const auto o=e.toObject();tx.replaceLeafPayload(o["path"].toString(),QByteArray::fromHex(o["sha256"].toString().toLatin1()),QByteArray::fromBase64(o["payload"].toString().toLatin1()));}
  if(!base.applyTransaction(tx,&bytes,&r))throw r.toText();
  CMobDocument moved;if(!moved.loadBytes(bytes,"moved.mob",&r))throw r.toText();
  if(p.contains("script")){if(!moved.replaceEmbeddedScript(p["script"].toString(),&bytes,&r))throw r.toText();if(!moved.loadBytes(bytes,"moved-script.mob",&r))throw r.toText();}
  QVector<QByteArray> raw,hashes;
  for(const auto& o:zone.objects())if(!o.unit&&o.sectionType!=0xBBAB0000){QByteArray b;if(!zone.objectSubtreeBytes(o.sectionPath,o.subtreeSha256,&b))throw o.sectionPath;raw.append(b);hashes.append(o.subtreeSha256);}
  for(const auto& o:moved.objects())if(o.unit||o.sectionType==0xBBAB0000){QByteArray b;if(!moved.objectSubtreeBytes(o.sectionPath,o.subtreeSha256,&b))throw o.sectionPath;raw.append(b);hashes.append(o.subtreeSha256);}
  if(!moved.rebuildObjectSequenceFromRaw(raw,hashes,&bytes,&r))throw r.toText();
  CMobDocument result;if(!result.loadBytes(bytes,"basecam.mob",&r)||r.hasErrors()||result.serialize()!=bytes)throw r.toText();
  QDir().mkpath(a[3]);write(a[3]+"/basecam.mob",bytes);
  auto terrain=mpr(p["zoneMpr"].toString());CMprEditTransaction mt;QByteArray mb=terrain.serialize();
  if(p.contains("namespace")&&p["namespace"].toString()!=terrain.mapInfo().innerNamespace)
   if(!terrain.applyIsolatedNamespaceTransaction(mt,p["namespace"].toString(),&mb,&r))throw r.toText();
  CMprDocument check;if(!check.loadBytes(mb,"basegipat.mpr",&r)||r.hasErrors()||check.serialize()!=mb)throw r.toText();
  write(a[3]+"/basegipat.mpr",mb);write(a[3]+"/native-validation.json",QJsonDocument(r.toJson()).toJson());
 } else throw QString("unknown mode");
 return 0;
 }catch(const QString& e){err<<e<<"\n";return 1;}
}
