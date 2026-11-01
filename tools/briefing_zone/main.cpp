#include <QtCore>
#include "endurance_document.h"
#include "mpr_document.h"
#include "res_archive_document.h"
#include "terrain_edit_model.h"
#include "map_table_document.h"

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
 if(a[1]=="catalog") {
  const auto p=QJsonDocument::fromJson(read(a[2])).object();const auto bytes=read(p["archive"].toString()),map=read(p["map"].toString());
  CResArchiveDocument archive;QStringList errors;if(!archive.loadBytes(bytes,"copied-texts.res",&errors))throw errors.join("\n");
  CMapTableDocument table;if(!table.loadBytes(map,&errors)||table.serialize()!=map)throw errors.join("\n");
  const SMapTableZone *base=nullptr,*zone=nullptr;for(const auto& z:table.zones()){if(z.zoneId=="bz1g")base=&z;if(z.zoneId=="gz1g")zone=&z;}
  if(!base||!zone||base->exits.size()<1||zone->exits.size()<2||base->exits[0].destinationZoneId!="gz1g"||base->exits[0].destinationExitId!=2||zone->exits[1].destinationZoneId!="bz1g"||zone->exits[1].destinationExitId!=1)throw QString("bz1g:1 <-> gz1g:2 pairing failed");
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
