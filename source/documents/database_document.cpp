#include "database_document.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QDataStream>
#include <QSaveFile>
#include <QSet>
#include <QTextCodec>
#include "lossless_database_member.h"
#include "res_archive_document.h"

namespace {
QString fold(const QString& v) { return v.trimmed().toLower(); }
ei::SDatabaseFieldSchema fs(const QString& key, const QString& label, ei::EDatabaseFieldType type, const QString& ref = QString())
{ ei::SDatabaseFieldSchema f; f.key=key; f.label=label; f.type=type; f.referenceTable=ref; return f; }
ei::SDatabaseFieldSchema ro(ei::SDatabaseFieldSchema f){f.writable=false;return f;}
QJsonValue jv(const QVariant& v) { return v.type()==QVariant::StringList ? QJsonValue(QJsonArray::fromStringList(v.toStringList())) : QJsonValue::fromVariant(v); }
QByteArray stringPayload(const QVariant& value)
{
    QByteArray out=QTextCodec::codecForName("CP1251")->fromUnicode(value.toString());out.append('\0');return out;
}
QByteArray intPayload(const QVariant& value)
{
    QByteArray out;QDataStream stream(&out,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);stream<<qint32(value.toInt());return out;
}
QByteArray floatPayload(const QVariant& value)
{
    QByteArray out;QDataStream stream(&out,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);stream<<float(value.toDouble());return out;
}
struct PatchTarget{QString member;int table{-1};int field{-1};enum Kind{String,Integer,Float,NestedStrings}kind{String};};
QByteArray nestedStringPayload(const QVariant& value){QByteArray out;for(const QString& text:value.toStringList()){const QByteArray payload=stringPayload(text);out.append(char(0));const quint32 encoded=quint32(payload.size())*2;if(encoded<=254)out.append(char(encoded));else{const quint32 longSize=encoded+1;out.append(char(longSize));out.append(char(longSize>>8));out.append(char(longSize>>16));out.append(char(longSize>>24));}out+=payload;}return out;}
bool patchTarget(const ei::SDatabaseChange& change,const ei::SDatabaseTable& table,PatchTarget* target)
{
    target->member=table.sourceMember;
    if(change.tableKey=="raceModels")
    {target->table=1;if(change.fieldKey=="model")target->field=30;else return false;target->kind=PatchTarget::String;return true;}
    if(change.tableKey=="monsters")
    {
        target->table=2;
        if(change.fieldKey=="baseRace"){target->field=1;target->kind=PatchTarget::String;}
        else if(change.fieldKey=="skinIndex"){target->field=3;target->kind=PatchTarget::Integer;}
        else if(change.fieldKey=="hairIndex"){target->field=4;target->kind=PatchTarget::Integer;}
        else if(change.fieldKey=="complexionX"){target->field=5;target->kind=PatchTarget::Float;}
        else if(change.fieldKey=="complexionY"){target->field=6;target->kind=PatchTarget::Float;}
        else if(change.fieldKey=="complexionZ"){target->field=7;target->kind=PatchTarget::Float;}
        else if(change.fieldKey=="items"){target->field=29;target->kind=PatchTarget::NestedStrings;}
        else if(change.fieldKey=="wears"){target->field=32;target->kind=PatchTarget::NestedStrings;}
        else if(change.fieldKey=="secondWeapon"){target->field=42;target->kind=PatchTarget::String;}
        else return false;
        return true;
    }
    if(change.tableKey=="items")
    {
        const auto* row=static_cast<const ei::SDatabaseRow*>(nullptr);
        for(const auto& candidate:table.rows)if(candidate.stableKey==change.rowKey){row=&candidate;break;}
        if(!row)return false;
        const QString category=row->values.value("category").toString();
        const QStringList categories={"materials","weapons","armor","quickItems","questItems","lootItems"};
        target->table=categories.indexOf(category);if(target->table<0)return false;
        if(change.fieldKey=="type"){target->field=1;target->kind=PatchTarget::String;}
        else if(change.fieldKey=="typeId"){target->field=2;target->kind=PatchTarget::Integer;}
        else if(change.fieldKey=="materialType"){target->field=3;target->kind=PatchTarget::String;}
        else if(change.fieldKey=="textureType1"){target->field=5;target->kind=PatchTarget::Integer;}
        else if(change.fieldKey=="textureType2"){target->field=6;target->kind=PatchTarget::Integer;}
        else if(change.fieldKey=="wearOrder"&&target->table==2){target->field=24;target->kind=PatchTarget::Integer;}
        else return false;
        return true;
    }
    return false;
}
}
void addOpaqueFields(ei::SDatabaseTable* table, ei::SDatabaseRow* row,
                     const QMap<int, QByteArray>& fields)
{
    for(auto it = fields.constBegin(); it != fields.constEnd(); ++it)
    {
        const QString key = QString("opaque.%1").arg(it.key());
        bool exists = false;
        for(const auto& schema : table->fields) exists = exists || schema.key == key;
        if(!exists)
        {
            auto schema = fs(key, QString("Unknown FLD%1 (raw)").arg(it.key()),
                             ei::EDatabaseFieldType::Text);
            schema.writable = false; schema.opaque = true; table->fields.append(schema);
        }
        row->values.insert(key, QString::fromLatin1(it.value().toHex()));
    }
}

namespace ei {
DatabaseDocument::DatabaseDocument(const QSharedPointer<const SProfileDatabaseSnapshot>& s):m_snapshot(s){}
QSharedPointer<DatabaseDocument> DatabaseDocument::fromSnapshot(const QSharedPointer<const SProfileDatabaseSnapshot>& s, QString* d)
{
    if(!s || !s->usable()) { if(d)*d="The active profile database is unavailable or invalid."; return {}; }
    QSharedPointer<DatabaseDocument> out(new DatabaseDocument(s));
    SDatabaseTable races; races.key="raceModels"; races.label="Race models"; races.sourceMember="units.udb";
    races.fields={ro(fs("name","Name",EDatabaseFieldType::Text)),fs("model","Model",EDatabaseFieldType::Text),ro(fs("primaryTextures","Primary textures",EDatabaseFieldType::TextList)),ro(fs("secondaryTextures","Secondary textures",EDatabaseFieldType::TextList))};
    for(const auto& x:s->raceModels){SDatabaseRow r;r.stableKey=fold(x.name);r.sourceStableKey=x.name;r.values={{"name",x.name},{"model",x.model},{"primaryTextures",x.primaryTextures},{"secondaryTextures",x.secondaryTextures}};addOpaqueFields(&races,&r,x.opaqueFields);races.rows<<r;} out->m_tables[races.key]=races;
    SDatabaseTable monsters; monsters.key="monsters"; monsters.label="Monster prototypes"; monsters.sourceMember="units.udb";
    monsters.fields={ro(fs("name","Name",EDatabaseFieldType::Text)),fs("baseRace","Base race",EDatabaseFieldType::Reference,"raceModels"),fs("skinIndex","Skin",EDatabaseFieldType::Integer),fs("hairIndex","Hair",EDatabaseFieldType::Integer),fs("complexionX","Complexion X",EDatabaseFieldType::Decimal),fs("complexionY","Complexion Y",EDatabaseFieldType::Decimal),fs("complexionZ","Complexion Z",EDatabaseFieldType::Decimal),fs("items","Items",EDatabaseFieldType::TextList),fs("wears","Wears",EDatabaseFieldType::TextList),ro(fs("weapon","Weapon",EDatabaseFieldType::Text)),fs("secondWeapon","Second weapon",EDatabaseFieldType::Text)};
    for(const auto& x:s->monsters){SDatabaseRow r;r.stableKey=fold(x.name);r.sourceStableKey=x.name;r.values={{"name",x.name},{"baseRace",x.baseRace},{"skinIndex",x.skinIndex},{"hairIndex",x.hairIndex},{"complexionX",x.complexionX},{"complexionY",x.complexionY},{"complexionZ",x.complexionZ},{"items",x.items},{"wears",x.wears},{"weapon",x.weapon},{"secondWeapon",x.secondWeapon}};addOpaqueFields(&monsters,&r,x.opaqueFields);monsters.rows<<r;} out->m_tables[monsters.key]=monsters;
    SDatabaseTable items; items.key="items"; items.label="Items"; items.sourceMember="items.idb";
    items.fields={ro(fs("name","Name",EDatabaseFieldType::Text)),ro(fs("category","Category",EDatabaseFieldType::Text)),fs("type","Type",EDatabaseFieldType::Text),fs("typeId","Type ID",EDatabaseFieldType::Integer),fs("materialType","Material type",EDatabaseFieldType::Text),fs("textureType1","Texture 1",EDatabaseFieldType::Integer),fs("textureType2","Texture 2",EDatabaseFieldType::Integer),fs("wearOrder","Wear order",EDatabaseFieldType::Integer)};
    for(const auto& x:s->items){SDatabaseRow r;r.stableKey=fold(x.name);r.sourceStableKey=x.name;r.values={{"name",x.name},{"category",x.category},{"type",x.type},{"typeId",x.typeId},{"materialType",x.materialType},{"textureType1",x.textureType1},{"textureType2",x.textureType2},{"wearOrder",x.wearOrder}};addOpaqueFields(&items,&r,x.opaqueFields);items.rows<<r;} out->m_tables[items.key]=items;
    out->m_baselineTables=out->m_tables;QFile sourceFile(s->sourceArchivePath);if(sourceFile.open(QIODevice::ReadOnly))out->m_sourceArchiveBaseline=sourceFile.readAll();out->m_expectedSourceSha256=s->sourceArchiveSha256;
    return out;
}
QString DatabaseDocument::profileId()const{return m_snapshot->profileId;} QString DatabaseDocument::providerId()const{return m_snapshot->providerId;}
QString DatabaseDocument::sourceArchivePath()const{return m_snapshot->sourceArchivePath;} QString DatabaseDocument::sourceArchiveSha256()const{return m_snapshot->sourceArchiveSha256;}
QStringList DatabaseDocument::tableKeys()const{return m_tables.keys();} const SDatabaseTable* DatabaseDocument::table(const QString& k)const{auto i=m_tables.constFind(k);return i==m_tables.constEnd()?nullptr:&i.value();}
bool DatabaseDocument::isDirty()const{return !m_undo.isEmpty();} bool DatabaseDocument::canUndo()const{return !m_undo.isEmpty();} bool DatabaseDocument::canRedo()const{return !m_redo.isEmpty();}
QVector<SDatabaseChange> DatabaseDocument::changes()const{QVector<SDatabaseChange> r;for(const auto&s:m_undo)r<<s.change;return r;}
SDatabaseRow* DatabaseDocument::row(const QString&t,const QString&k){auto i=m_tables.find(t);if(i==m_tables.end())return nullptr;for(auto&r:i->rows)if(r.stableKey==fold(k))return &r;return nullptr;}
void DatabaseDocument::commit(const QMap<QString,SDatabaseTable>&b,const SDatabaseChange&c){m_undo.append({b,c});m_redo.clear();}
bool DatabaseDocument::setValue(const QString&t,const QString&r,const QString&f,const QVariant&v,QString*d)
{SDatabaseRow*x=row(t,r);const SDatabaseTable*tb=table(t);if(!x||!tb){if(d)*d="Row is unavailable.";return false;}bool known=false;for(const auto&s:tb->fields)known|=s.key==f&&s.writable;if(!known){if(d)*d="Field is unsupported or read-only.";return false;}if(f=="name"){QString k=fold(v.toString());if(k.isEmpty()){if(d)*d="Stable name cannot be empty.";return false;}for(const auto&c:tb->rows)if(&c!=x&&!c.deleted&&c.stableKey==k){if(d)*d="Stable name collides with an existing row.";return false;}}QVariant old=x->values.value(f);if(old==v)return true;auto before=m_tables;x=row(t,r);x->values[f]=v;if(f=="name")x->stableKey=fold(v.toString());commit(before,{t,r,f,old,v,QString("Changed %1.%2").arg(r,f)});return true;}
bool DatabaseDocument::cloneRow(const QString&t,const QString&r,const QString&n,QString*d){SDatabaseRow*s=row(t,r);QString k=fold(n);if(!s||m_tables.find(t)==m_tables.end()||k.isEmpty()||row(t,k)){if(d)*d="New stable name is empty or already exists.";return false;}SDatabaseRow c=*s;auto before=m_tables;auto tb=m_tables.find(t);c.sourceStableKey=s->sourceStableKey.isEmpty()?s->stableKey:s->sourceStableKey;c.stableKey=k;c.values["name"]=n;c.added=true;c.deleted=false;tb->rows<<c;commit(before,{t,k,{}, {},n,QString("Cloned %1 as %2").arg(r,n)});return true;}
bool DatabaseDocument::deleteRow(const QString&t,const QString&r,QString*d){auto*x=row(t,r);if(!x||x->deleted){if(d)*d="Row is unavailable.";return false;}auto before=m_tables;x=row(t,r);x->deleted=true;commit(before,{t,r,{},false,true,QString("Deleted %1").arg(r)});return true;}
bool DatabaseDocument::undo(){if(m_undo.isEmpty())return false;auto s=m_undo.takeLast();m_redo.append({m_tables,s.change});m_tables=s.tables;return true;} bool DatabaseDocument::redo(){if(m_redo.isEmpty())return false;auto s=m_redo.takeLast();m_undo.append({m_tables,s.change});m_tables=s.tables;return true;}
QVector<SDatabaseValidationFinding> DatabaseDocument::validate()const{QVector<SDatabaseValidationFinding>o;for(const auto&tb:m_tables){QMap<QString,int>ids;for(const auto&r:tb.rows){if(r.deleted)continue;QString n=fold(r.values.value("name").toString());if(n.isEmpty()||++ids[n]>1)o.append({SDatabaseValidationFinding::Severity::Error,"database.identity.invalid",tb.key,r.stableKey,"Stable name is empty or duplicated.",true});for(const auto&f:tb.fields){if(f.referenceTable.isEmpty())continue;QString ref=fold(r.values.value(f.key).toString());const auto*target=table(f.referenceTable);bool found=ref.isEmpty();if(target)for(const auto&c:target->rows)found|=!c.deleted&&c.stableKey==ref;if(!found)o.append({SDatabaseValidationFinding::Severity::Error,"database.reference.missing",tb.key,r.stableKey,QString("%1 references missing %2 row '%3'.").arg(f.label,f.referenceTable,ref),true});}}}if(isDirty())o.append({SDatabaseValidationFinding::Severity::Warning,"database.impact.opaque",{}, {},"MOB, texts.res, and opaque table references require external impact review before publication.",false});return o;}
QJsonObject DatabaseDocument::pendingChangeManifest()const{QJsonObject o;o["format"]="ei-mapper-database-change-set-v1";o["providerId"]=providerId();o["profileId"]=profileId();o["sourceArchive"]=sourceArchivePath();o["sourceSha256"]=sourceArchiveSha256();QString publicationDiagnostic;o["publicationSupported"]=!buildArchive(&publicationDiagnostic).isEmpty();o["publicationDiagnostic"]=publicationDiagnostic;QJsonArray a;for(const auto&c:changes()){QJsonObject x;x["table"]=c.tableKey;x["row"]=c.rowKey;x["field"]=c.fieldKey;x["before"]=jv(c.before);x["after"]=jv(c.after);x["description"]=c.description;a<<x;}o["changes"]=a;QJsonArray p;for(auto i=m_snapshot->rawMembers.cbegin();i!=m_snapshot->rawMembers.cend();++i)p<<i.key();o["preservedRawMembers"]=p;return o;}
bool DatabaseDocument::sourceIsCurrent(QString*d)const{QFile f(sourceArchivePath());if(!f.open(QIODevice::ReadOnly)){if(d)*d="Source archive is no longer readable.";return false;}QString h=QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex());if(h.compare(m_expectedSourceSha256,Qt::CaseInsensitive)!=0){if(d)*d="Source archive changed outside this database editor session.";return false;}return true;}
QByteArray DatabaseDocument::buildArchive(QString* diagnostic)const
{
    QByteArray archiveBytes=m_sourceArchiveBaseline;if(archiveBytes.isEmpty()){if(diagnostic)*diagnostic="Source archive baseline is unavailable.";return {};}
    if(changes().isEmpty())return archiveBytes;
    if(!sourceIsCurrent(diagnostic))return {};
    QMap<QString,LosslessDatabaseMember> members;
    for(auto tableIt=m_tables.constBegin();tableIt!=m_tables.constEnd();++tableIt)
    {
        const SDatabaseTable& table=tableIt.value();const SDatabaseTable baseline=m_baselineTables.value(table.key);
        bool tableChanged=false;for(const auto& row:table.rows)tableChanged|=row.added||row.deleted;for(const auto& row:table.rows)if(!row.deleted){const SDatabaseRow* base=nullptr;for(const auto& candidate:baseline.rows)if(candidate.stableKey==row.stableKey){base=&candidate;break;}if(base)for(const auto& field:table.fields)tableChanged|=field.writable&&base->values.value(field.key)!=row.values.value(field.key);}
        if(!tableChanged)continue;QString parseDiagnostic;auto member=LosslessDatabaseMember::parse(m_snapshot->rawMembers.value(table.sourceMember),&parseDiagnostic);if(!member.valid()){if(diagnostic)*diagnostic=parseDiagnostic;return {};}
        SDatabaseChange probe;probe.tableKey=table.key;probe.rowKey=table.rows.isEmpty()?QString():table.rows.first().stableKey;probe.fieldKey=table.key=="raceModels"?"model":(table.key=="monsters"?"skinIndex":"typeId");PatchTarget structural;if(!patchTarget(probe,table,&structural)){if(diagnostic)*diagnostic="Table publication mapping is unavailable.";return {};}
        for(const auto& row:table.rows)if(row.added&&!row.deleted){QString e;if(!member.cloneRecordByFieldPayload(structural.table,0,stringPayload(row.sourceStableKey),stringPayload(row.values.value("name")),&e)){if(diagnostic)*diagnostic=e;return {};}}
        for(const auto& row:table.rows)if(row.deleted&&!row.added){QString e;if(!member.removeRecordByFieldPayload(structural.table,0,stringPayload(row.values.value("name")),&e)){if(diagnostic)*diagnostic=e;return {};}}
        for(const auto& row:table.rows)if(!row.deleted)for(const auto& field:table.fields)if(field.writable)
        {
            const SDatabaseRow* base=nullptr;const QString baselineKey=row.added?fold(row.sourceStableKey):row.stableKey;for(const auto& candidate:baseline.rows)if(candidate.stableKey==baselineKey){base=&candidate;break;}
            if(base&&base->values.value(field.key)==row.values.value(field.key))continue;
            SDatabaseChange change;change.tableKey=table.key;change.rowKey=row.stableKey;change.fieldKey=field.key;change.after=row.values.value(field.key);PatchTarget target;if(!patchTarget(change,table,&target)){if(diagnostic)*diagnostic=QString("Field %1 is visible but not yet publishable.").arg(field.key);return {};}
            int record=member.findRecordByFieldPayload(target.table,0,stringPayload(row.values.value("name")));if(record<0){if(diagnostic)*diagnostic="Stable row identity is missing or ambiguous.";return {};}
            const QByteArray payload=target.kind==PatchTarget::String?stringPayload(change.after):(target.kind==PatchTarget::Integer?intPayload(change.after):(target.kind==PatchTarget::Float?floatPayload(change.after):nestedStringPayload(change.after)));QString e;if(!member.replaceFieldPayload(target.table,record,target.field,0,payload,&e)){if(diagnostic)*diagnostic=e;return {};}
        }
        members.insert(table.sourceMember,member);
    }
    for(auto it=members.constBegin();it!=members.constEnd();++it)
    {
        CResArchiveDocument archive;QStringList errors;if(!archive.loadBytes(archiveBytes,sourceArchivePath(),&errors)){if(diagnostic)*diagnostic=errors.join('\n');return {};}
        QByteArray replaced;const QByteArray expected=QCryptographicHash::hash(archive.memberPayload(it.key()),QCryptographicHash::Sha256).toHex();
        if(!archive.replaceMember(it.key(),expected,it.value().serialize(),&replaced,&errors)){if(diagnostic)*diagnostic=errors.join('\n');return {};}
        archiveBytes=replaced;
    }
    return archiveBytes;
}
bool DatabaseDocument::replaceTables(const QMap<QString,SDatabaseTable>& tables,const QString& description,QString* diagnostic)
{
    const auto before=m_tables;const auto baselineFindings=validate();QSet<QString> baselineErrors;for(const auto& finding:baselineFindings)if(finding.severity==SDatabaseValidationFinding::Severity::Error)baselineErrors.insert(finding.code+'|'+finding.tableKey+'|'+finding.rowKey+'|'+finding.message);m_tables=tables;QSet<QString> candidateErrors;for(const auto& finding:validate())if(finding.severity==SDatabaseValidationFinding::Severity::Error)candidateErrors.insert(finding.code+'|'+finding.tableKey+'|'+finding.rowKey+'|'+finding.message);for(const QString& error:candidateErrors)if(!baselineErrors.contains(error)){m_tables=before;if(diagnostic)*diagnostic=error.section('|',3);return false;}commit(before,{"workbook",{}, {}, {}, {},description});return true;
}
QSharedPointer<const SProfileDatabaseSnapshot> DatabaseDocument::currentSnapshot(QString* diagnostic)const
{
    const QByteArray archive=buildArchive(diagnostic);if(archive.isEmpty())return {};CResArchiveDocument res;QStringList errors;if(!res.loadBytes(archive,sourceArchivePath(),&errors)){if(diagnostic)*diagnostic=errors.join('\n');return {};}
    QMap<QString,QByteArray> raw;for(const auto& member:res.members())raw.insert(member.name,member.payload);const auto parsed=parseProfileDatabaseMembers(raw,sourceArchivePath());QSharedPointer<SProfileDatabaseSnapshot> out(new SProfileDatabaseSnapshot(*m_snapshot));out->rawMembers=raw;out->monsters=parsed.monsters;out->raceModels=parsed.raceModels;out->items=parsed.items;out->materialRecords=parsed.materialRecords;out->materials=parsed.namesByCategory.value("materials");out->weapons=parsed.namesByCategory.value("weapons");out->armor=parsed.namesByCategory.value("armor");out->quickItems=parsed.namesByCategory.value("quickItems");out->questItems=parsed.namesByCategory.value("questItems");out->lootItems=parsed.namesByCategory.value("lootItems");return out;
}
bool DatabaseDocument::saveArchive(const QString& path,QString* diagnostic)const
{
    const QByteArray bytes=buildArchive(diagnostic);if(bytes.isEmpty())return false;QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){if(diagnostic)*diagnostic=file.errorString();return false;}if(file.write(bytes)!=bytes.size()){if(diagnostic)*diagnostic=file.errorString();return false;}if(!file.commit()){if(diagnostic)*diagnostic=file.errorString();return false;}return true;
}
bool DatabaseDocument::applyToSource(QString* diagnostic){if(!sourceIsCurrent(diagnostic))return false;const QByteArray bytes=buildArchive(diagnostic);if(bytes.isEmpty())return false;QSaveFile file(sourceArchivePath());if(!file.open(QIODevice::WriteOnly)){if(diagnostic)*diagnostic=file.errorString();return false;}if(file.write(bytes)!=bytes.size()||!file.commit()){if(diagnostic)*diagnostic=file.errorString();return false;}m_expectedSourceSha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());return true;}
}
