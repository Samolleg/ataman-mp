#include "database_document.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>

namespace {
QString fold(const QString& v) { return v.trimmed().toLower(); }
ei::SDatabaseFieldSchema fs(const QString& key, const QString& label, ei::EDatabaseFieldType type, const QString& ref = QString())
{ ei::SDatabaseFieldSchema f; f.key=key; f.label=label; f.type=type; f.referenceTable=ref; return f; }
QJsonValue jv(const QVariant& v) { return v.type()==QVariant::StringList ? QJsonValue(QJsonArray::fromStringList(v.toStringList())) : QJsonValue::fromVariant(v); }
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
    races.fields={fs("name","Name",EDatabaseFieldType::Text),fs("model","Model",EDatabaseFieldType::Text),fs("primaryTextures","Primary textures",EDatabaseFieldType::TextList),fs("secondaryTextures","Secondary textures",EDatabaseFieldType::TextList)};
    for(const auto& x:s->raceModels){SDatabaseRow r;r.stableKey=fold(x.name);r.values={{"name",x.name},{"model",x.model},{"primaryTextures",x.primaryTextures},{"secondaryTextures",x.secondaryTextures}};addOpaqueFields(&races,&r,x.opaqueFields);races.rows<<r;} out->m_tables[races.key]=races;
    SDatabaseTable monsters; monsters.key="monsters"; monsters.label="Monster prototypes"; monsters.sourceMember="units.udb";
    monsters.fields={fs("name","Name",EDatabaseFieldType::Text),fs("baseRace","Base race",EDatabaseFieldType::Reference,"raceModels"),fs("skinIndex","Skin",EDatabaseFieldType::Integer),fs("hairIndex","Hair",EDatabaseFieldType::Integer),fs("complexionX","Complexion X",EDatabaseFieldType::Decimal),fs("complexionY","Complexion Y",EDatabaseFieldType::Decimal),fs("complexionZ","Complexion Z",EDatabaseFieldType::Decimal),fs("items","Items",EDatabaseFieldType::TextList),fs("wears","Wears",EDatabaseFieldType::TextList),fs("weapon","Weapon",EDatabaseFieldType::Text),fs("secondWeapon","Second weapon",EDatabaseFieldType::Text)};
    for(const auto& x:s->monsters){SDatabaseRow r;r.stableKey=fold(x.name);r.values={{"name",x.name},{"baseRace",x.baseRace},{"skinIndex",x.skinIndex},{"hairIndex",x.hairIndex},{"complexionX",x.complexionX},{"complexionY",x.complexionY},{"complexionZ",x.complexionZ},{"items",x.items},{"wears",x.wears},{"weapon",x.weapon},{"secondWeapon",x.secondWeapon}};addOpaqueFields(&monsters,&r,x.opaqueFields);monsters.rows<<r;} out->m_tables[monsters.key]=monsters;
    SDatabaseTable items; items.key="items"; items.label="Items"; items.sourceMember="items.idb";
    items.fields={fs("name","Name",EDatabaseFieldType::Text),fs("category","Category",EDatabaseFieldType::Text),fs("type","Type",EDatabaseFieldType::Text),fs("typeId","Type ID",EDatabaseFieldType::Integer),fs("materialType","Material type",EDatabaseFieldType::Text),fs("textureType1","Texture 1",EDatabaseFieldType::Integer),fs("textureType2","Texture 2",EDatabaseFieldType::Integer),fs("wearOrder","Wear order",EDatabaseFieldType::Integer)};
    for(const auto& x:s->items){SDatabaseRow r;r.stableKey=fold(x.name);r.values={{"name",x.name},{"category",x.category},{"type",x.type},{"typeId",x.typeId},{"materialType",x.materialType},{"textureType1",x.textureType1},{"textureType2",x.textureType2},{"wearOrder",x.wearOrder}};addOpaqueFields(&items,&r,x.opaqueFields);items.rows<<r;} out->m_tables[items.key]=items;
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
{SDatabaseRow*x=row(t,r);const SDatabaseTable*tb=table(t);if(!x||!tb){if(d)*d="Row is unavailable.";return false;}bool known=false;for(const auto&s:tb->fields)known|=s.key==f&&s.writable;if(!known){if(d)*d="Field is unsupported or read-only.";return false;}if(f=="name"){QString k=fold(v.toString());if(k.isEmpty()){if(d)*d="Stable name cannot be empty.";return false;}for(const auto&c:tb->rows)if(&c!=x&&!c.deleted&&c.stableKey==k){if(d)*d="Stable name collides with an existing row.";return false;}}QVariant old=x->values.value(f);if(old==v)return true;auto before=m_tables;x->values[f]=v;if(f=="name")x->stableKey=fold(v.toString());commit(before,{t,r,f,old,v,QString("Changed %1.%2").arg(r,f)});return true;}
bool DatabaseDocument::cloneRow(const QString&t,const QString&r,const QString&n,QString*d){SDatabaseRow*s=row(t,r);auto tb=m_tables.find(t);QString k=fold(n);if(!s||tb==m_tables.end()||k.isEmpty()||row(t,k)){if(d)*d="New stable name is empty or already exists.";return false;}auto before=m_tables;SDatabaseRow c=*s;c.stableKey=k;c.values["name"]=n;c.added=true;c.deleted=false;tb->rows<<c;commit(before,{t,k,{}, {},n,QString("Cloned %1 as %2").arg(r,n)});return true;}
bool DatabaseDocument::deleteRow(const QString&t,const QString&r,QString*d){auto*x=row(t,r);if(!x||x->deleted){if(d)*d="Row is unavailable.";return false;}auto before=m_tables;x->deleted=true;commit(before,{t,r,{},false,true,QString("Deleted %1").arg(r)});return true;}
bool DatabaseDocument::undo(){if(m_undo.isEmpty())return false;auto s=m_undo.takeLast();m_redo.append({m_tables,s.change});m_tables=s.tables;return true;} bool DatabaseDocument::redo(){if(m_redo.isEmpty())return false;auto s=m_redo.takeLast();m_undo.append({m_tables,s.change});m_tables=s.tables;return true;}
QVector<SDatabaseValidationFinding> DatabaseDocument::validate()const{QVector<SDatabaseValidationFinding>o;for(const auto&tb:m_tables){QMap<QString,int>ids;for(const auto&r:tb.rows){if(r.deleted)continue;QString n=fold(r.values.value("name").toString());if(n.isEmpty()||++ids[n]>1)o.append({SDatabaseValidationFinding::Severity::Error,"database.identity.invalid",tb.key,r.stableKey,"Stable name is empty or duplicated.",true});for(const auto&f:tb.fields){if(f.referenceTable.isEmpty())continue;QString ref=fold(r.values.value(f.key).toString());const auto*target=table(f.referenceTable);bool found=ref.isEmpty();if(target)for(const auto&c:target->rows)found|=!c.deleted&&c.stableKey==ref;if(!found)o.append({SDatabaseValidationFinding::Severity::Error,"database.reference.missing",tb.key,r.stableKey,QString("%1 references missing %2 row '%3'.").arg(f.label,f.referenceTable,ref),true});}}}if(isDirty())o.append({SDatabaseValidationFinding::Severity::Warning,"database.impact.opaque",{}, {},"MOB, texts.res, and opaque table references require external impact review before publication.",false});return o;}
QJsonObject DatabaseDocument::pendingChangeManifest()const{QJsonObject o;o["format"]="ei-mapper-database-change-set-v1";o["providerId"]=providerId();o["profileId"]=profileId();o["sourceArchive"]=sourceArchivePath();o["sourceSha256"]=sourceArchiveSha256();o["publicationSupported"]=false;QJsonArray a;for(const auto&c:changes()){QJsonObject x;x["table"]=c.tableKey;x["row"]=c.rowKey;x["field"]=c.fieldKey;x["before"]=jv(c.before);x["after"]=jv(c.after);x["description"]=c.description;a<<x;}o["changes"]=a;QJsonArray p;for(auto i=m_snapshot->rawMembers.cbegin();i!=m_snapshot->rawMembers.cend();++i)p<<i.key();o["preservedRawMembers"]=p;return o;}
bool DatabaseDocument::sourceIsCurrent(QString*d)const{QFile f(sourceArchivePath());if(!f.open(QIODevice::ReadOnly)){if(d)*d="Source archive is no longer readable.";return false;}QString h=QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex());if(h!=sourceArchiveSha256()){if(d)*d="Source archive changed since the document was opened.";return false;}return true;}
}
