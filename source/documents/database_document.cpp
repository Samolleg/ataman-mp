#include "database_document.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QDataStream>
#include <QSaveFile>
#include <QSet>
#include <QTextCodec>
#include <cmath>
#include "lossless_database_member.h"
#include "database_schema.h"
#include "database_value_codec.h"
#include "res_archive_document.h"

namespace {
QString fold(const QString& v) { return v.trimmed().toLower(); }
bool sameValue(const QVariant& a,const QVariant& b)
{
    if(a.type()!=b.type())return a.toString()==b.toString();
    if(a.type()==QVariant::Double){const double x=a.toDouble(),y=b.toDouble();return (std::isnan(x)&&std::isnan(y))||x==y;}
    if(a.userType()==QMetaType::Float){const float x=a.toFloat(),y=b.toFloat();return (std::isnan(x)&&std::isnan(y))||x==y;}
    return a==b;
}
ei::SDatabaseFieldSchema fs(const QString& key, const QString& label, ei::EDatabaseFieldType type, const QString& ref = QString())
{ ei::SDatabaseFieldSchema f; f.key=key; f.label=label; f.type=type; f.referenceTable=ref; return f; }
ei::SDatabaseFieldSchema ro(ei::SDatabaseFieldSchema f){f.writable=false;return f;}
ei::EDatabaseFieldType displayType(ei::EDatabaseStorageType type)
{
    if(type==ei::EDatabaseStorageType::UnsignedLong||type==ei::EDatabaseStorageType::SignedLong||type==ei::EDatabaseStorageType::Byte)return ei::EDatabaseFieldType::Integer;
    if(type==ei::EDatabaseStorageType::Float)return ei::EDatabaseFieldType::Decimal;
    return ei::EDatabaseFieldType::Text;
}
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
QVector<int> nestedFieldIds(const QByteArray& source)
{
    QVector<int> ids;
    int offset = 0;
    while(offset + 2 <= source.size())
    {
        const int id = uchar(source.at(offset));
        quint32 encoded = uchar(source.at(offset + 1));
        int header = 2;
        if(encoded & 1)
        {
            if(offset + 5 > source.size())
                return {};
            encoded = quint32(uchar(source.at(offset + 1)))
                | (quint32(uchar(source.at(offset + 2))) << 8)
                | (quint32(uchar(source.at(offset + 3))) << 16)
                | (quint32(uchar(source.at(offset + 4))) << 24);
            encoded -= 1;
            header = 5;
        }
        const int length = int(encoded / 2);
        if(length < 0 || offset + header + length > source.size())
            return {};
        ids.append(id);
        offset += header + length;
    }
    return offset == source.size() ? ids : QVector<int>();
}
QByteArray nestedStringPayload(const QVariant& value, const QByteArray& source)
{
    QByteArray out;
    const QVector<int> sourceIds = nestedFieldIds(source);
    const QStringList strings = value.toStringList();
    for(int index = 0; index < strings.size(); ++index)
    {
        const QByteArray payload = stringPayload(strings.at(index));
        const int id = sourceIds.isEmpty() ? 0
            : sourceIds.at(qMin(index, sourceIds.size() - 1));
        out.append(char(id));
        const quint32 encoded=quint32(payload.size())*2;
        if(encoded<=254)out.append(char(encoded));else{const quint32 longSize=encoded+1;out.append(char(longSize));out.append(char(longSize>>8));out.append(char(longSize>>16));out.append(char(longSize>>24));}out+=payload;
    }
    return out;
}
bool patchTarget(const ei::SDatabaseChange& change,const ei::SDatabaseTable& table,PatchTarget* target)
{
    target->member=table.sourceMember;
    if(change.tableKey=="raceModels")
    {target->table=1;if(change.fieldKey=="name")target->field=0;else if(change.fieldKey=="model")target->field=30;else return false;target->kind=PatchTarget::String;return true;}
    if(change.tableKey=="monsters")
    {
        target->table=2;
        if(change.fieldKey=="name"){target->field=0;target->kind=PatchTarget::String;}
        else if(change.fieldKey=="baseRace"){target->field=1;target->kind=PatchTarget::String;}
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
        if(change.fieldKey=="name"){target->field=0;target->kind=PatchTarget::String;}
        else if(change.fieldKey=="type"){target->field=1;target->kind=PatchTarget::String;}
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

QString decodedRawIdentity(const QByteArray& payload)
{
    if(payload.isEmpty() || payload.size() > 512)
        return {};
    QByteArray text = payload;
    while(!text.isEmpty() && text.endsWith('\0'))
        text.chop(1);
    if(text.isEmpty())
        return {};
    int controls = 0;
    for(const uchar byte : text)
        controls += byte < 0x20 && byte != '\t';
    if(controls * 8 > text.size())
        return {};
    return QTextCodec::codecForName("CP1251")->toUnicode(text);
}

void addRawDatabaseTables(const QSharedPointer<const ei::SProfileDatabaseSnapshot>& snapshot,
                          QMap<QString, ei::SDatabaseTable>* tables)
{
    for(auto memberIt = snapshot->rawMembers.constBegin();
        memberIt != snapshot->rawMembers.constEnd(); ++memberIt)
    {
        const QString member = memberIt.key();
        const QString suffix = QFileInfo(member).suffix().toLower();
        if(!(suffix == "db" || suffix == "idb" || suffix == "ldb"
             || suffix == "pdb" || suffix == "qdb" || suffix == "sdb"
             || suffix == "udb"))
            continue;
        QString diagnostic;
        const auto parsed = ei::LosslessDatabaseMember::parse(memberIt.value(), &diagnostic);
        if(!parsed.valid())
            continue;
        for(int tableIndex = 0; tableIndex < parsed.tableCount(); ++tableIndex)
        {
            ei::SDatabaseTable table;
            table.key = QString("raw.%1.%2").arg(member.toLower()).arg(tableIndex);
            table.label = QString("%1 — table %2 (complete/raw)").arg(member).arg(tableIndex);
            table.sourceMember = member;
            table.structuralEditing = false;
            QSet<QString> fieldKeys;
            for(int recordIndex = 0; recordIndex < parsed.recordCount(tableIndex); ++recordIndex)
                for(const auto& field : parsed.fields(tableIndex, recordIndex))
                    fieldKeys.insert(QString("fld.%1.%2").arg(field.id).arg(field.occurrence));
            QStringList ordered = fieldKeys.values();
            std::sort(ordered.begin(), ordered.end(), [](const QString& left, const QString& right)
            {
                const QStringList a = left.split('.'), b = right.split('.');
                const int aid = a.value(1).toInt(), bid = b.value(1).toInt();
                return aid == bid ? a.value(2).toInt() < b.value(2).toInt() : aid < bid;
            });
            for(const QString& key : ordered)
            {
                const QStringList parts = key.split('.');
                auto schema = fs(key, QString("FLD%1-%2 (raw)")
                    .arg(parts.value(1)).arg(parts.value(2)), ei::EDatabaseFieldType::Text);
                schema.writable = false;
                schema.opaque = true;
                table.fields.append(schema);
            }
            for(int recordIndex = 0; recordIndex < parsed.recordCount(tableIndex); ++recordIndex)
            {
                ei::SDatabaseRow row;
                const auto fields = parsed.fields(tableIndex, recordIndex);
                QString identity;
                for(const auto& field : fields)
                {
                    const QString key = QString("fld.%1.%2").arg(field.id).arg(field.occurrence);
                    row.values.insert(key, QString::fromLatin1(field.payload.toHex()));
                    if(field.id == 0 && field.occurrence == 0)
                        identity = decodedRawIdentity(field.payload);
                }
                row.stableKey = QString("record.%1").arg(recordIndex);
                row.sourceStableKey = identity.isEmpty()
                    ? QString("Record %1").arg(recordIndex) : identity;
                table.rows.append(row);
            }
            tables->insert(table.key, table);
        }
    }
}

QString sourceIdentity(const QVector<ei::SLosslessDatabaseField>& fields,int id)
{
    for(const auto& field:fields)if(field.id==id&&field.occurrence==0)return decodedRawIdentity(field.payload);
    return {};
}
void addSchemaDatabaseTables(const QSharedPointer<const ei::SProfileDatabaseSnapshot>& snapshot,
                             QMap<QString,ei::SDatabaseTable>* tables,QString* diagnostic)
{
    const auto& schema=ei::DatabaseSchema::instance();if(!schema.diagnostic().isEmpty()){if(diagnostic)*diagnostic=schema.diagnostic();return;}
    for(const auto& storage:schema.tables())
    {
        const QByteArray bytes=snapshot->rawMembers.value(storage.memberName);if(bytes.isEmpty())continue;QString parseDiagnostic;const auto member=ei::LosslessDatabaseMember::parse(bytes,&parseDiagnostic);if(!member.valid()||storage.physicalIndex>=member.tableCount()){if(diagnostic)*diagnostic=parseDiagnostic;continue;}
        ei::SDatabaseTable table;table.key=storage.key;table.label=storage.title;table.sourceMember=storage.memberName;table.schemaFamily=storage.family;table.physicalTable=storage.physicalIndex;table.blockId=storage.blockId;
        for(const auto& storageField:storage.fields)
        {
            QVector<ei::SDatabaseStorageHeader> headers=schema.headers(storage.family,storage.blockId,storageField.id);int occurrences=1;
            for(int occurrence=0;occurrence<occurrences;++occurrence){ei::SDatabaseFieldSchema field;field.key=QString("fld.%1.%2").arg(storageField.id).arg(occurrence);const auto matches=schema.headers(storage.family,storage.blockId,storageField.id);field.label=matches.value(occurrence).name;if(field.label.isEmpty())field.label=QString("FLD%1-%2").arg(storageField.id).arg(occurrence);field.type=displayType(storageField.type);field.storageId=storageField.id;field.storageOccurrence=occurrence;field.opaque=storageField.type==ei::EDatabaseStorageType::Hex;table.fields.append(field);}
        }
        table.identityFieldId=-1;for(const auto& field:storage.fields)if(field.id==0&&field.type==ei::EDatabaseStorageType::String){table.identityFieldId=0;break;}if(table.identityFieldId<0)for(const auto& field:storage.fields)if((field.id==0||field.id==1)&&field.type==ei::EDatabaseStorageType::String){table.identityFieldId=field.id;break;}
        for(int recordIndex=0;recordIndex<member.recordCount(storage.physicalIndex);++recordIndex)
        {
            const auto sourceFields=member.fields(storage.physicalIndex,recordIndex);ei::SDatabaseRow row;row.sourceRecordIndex=recordIndex;row.stableKey=QString("record.%1").arg(recordIndex);row.sourceStableKey=sourceIdentity(sourceFields,table.identityFieldId);if(!row.sourceStableKey.isEmpty())row.stableKey=fold(row.sourceStableKey)+QString("#%1").arg(recordIndex);
            for(const auto& field:sourceFields)
            {
                const auto* storageField=schema.field(storage.family,storage.blockId,field.id);if(!storageField)continue;QString valueDiagnostic;const QVariant decoded=ei::DatabaseValueCodec::decode(field.payload,*storageField,schema,storage.family,&valueDiagnostic);const QString key=QString("fld.%1.%2").arg(field.id).arg(field.occurrence);row.values[key]=decoded;
            }
            table.rows.append(row);
        }
        tables->insert(table.key,table);
    }
}

namespace ei {
DatabaseDocument::DatabaseDocument(const QSharedPointer<const SProfileDatabaseSnapshot>& s):m_snapshot(s){}
QSharedPointer<DatabaseDocument> DatabaseDocument::fromSnapshot(const QSharedPointer<const SProfileDatabaseSnapshot>& s, QString* d)
{
    if(!s || !s->usable()) { if(d)*d="The active profile database is unavailable or invalid."; return {}; }
    QSharedPointer<DatabaseDocument> out(new DatabaseDocument(s));
    SDatabaseTable races; races.key="raceModels"; races.label="Race models"; races.sourceMember="units.udb";
    races.fields={fs("name","Name / ID",EDatabaseFieldType::Text),fs("model","Model",EDatabaseFieldType::Text),ro(fs("primaryTextures","Primary textures",EDatabaseFieldType::TextList)),ro(fs("secondaryTextures","Secondary textures",EDatabaseFieldType::TextList))};
    for(const auto& x:s->raceModels){SDatabaseRow r;r.stableKey=fold(x.name);r.sourceStableKey=x.name;r.values={{"name",x.name},{"model",x.model},{"primaryTextures",x.primaryTextures},{"secondaryTextures",x.secondaryTextures}};addOpaqueFields(&races,&r,x.opaqueFields);races.rows<<r;} out->m_tables[races.key]=races;
    SDatabaseTable monsters; monsters.key="monsters"; monsters.label="Monster prototypes"; monsters.sourceMember="units.udb";
    monsters.fields={fs("name","Name / ID",EDatabaseFieldType::Text),fs("baseRace","Base race",EDatabaseFieldType::Reference,"raceModels"),fs("skinIndex","Skin",EDatabaseFieldType::Integer),fs("hairIndex","Hair",EDatabaseFieldType::Integer),fs("complexionX","Complexion X",EDatabaseFieldType::Decimal),fs("complexionY","Complexion Y",EDatabaseFieldType::Decimal),fs("complexionZ","Complexion Z",EDatabaseFieldType::Decimal),fs("items","Items",EDatabaseFieldType::TextList),fs("wears","Wears",EDatabaseFieldType::TextList),ro(fs("weapon","Weapon",EDatabaseFieldType::Text)),fs("secondWeapon","Second weapon",EDatabaseFieldType::Text)};
    for(const auto& x:s->monsters){SDatabaseRow r;r.stableKey=fold(x.name);r.sourceStableKey=x.name;r.values={{"name",x.name},{"baseRace",x.baseRace},{"skinIndex",x.skinIndex},{"hairIndex",x.hairIndex},{"complexionX",x.complexionX},{"complexionY",x.complexionY},{"complexionZ",x.complexionZ},{"items",x.items},{"wears",x.wears},{"weapon",x.weapon},{"secondWeapon",x.secondWeapon}};addOpaqueFields(&monsters,&r,x.opaqueFields);monsters.rows<<r;} out->m_tables[monsters.key]=monsters;
    SDatabaseTable items; items.key="items"; items.label="Items"; items.sourceMember="items.idb";
    items.fields={fs("name","Name / ID",EDatabaseFieldType::Text),ro(fs("category","Category",EDatabaseFieldType::Text)),fs("type","Type",EDatabaseFieldType::Text),fs("typeId","Type ID",EDatabaseFieldType::Integer),fs("materialType","Material type",EDatabaseFieldType::Text),fs("textureType1","Texture 1",EDatabaseFieldType::Integer),fs("textureType2","Texture 2",EDatabaseFieldType::Integer),fs("wearOrder","Wear order",EDatabaseFieldType::Integer)};
    for(const auto& x:s->items){SDatabaseRow r;r.stableKey=fold(x.name);r.sourceStableKey=x.name;r.values={{"name",x.name},{"category",x.category},{"type",x.type},{"typeId",x.typeId},{"materialType",x.materialType},{"textureType1",x.textureType1},{"textureType2",x.textureType2},{"wearOrder",x.wearOrder}};addOpaqueFields(&items,&r,x.opaqueFields);items.rows<<r;} out->m_tables[items.key]=items;
    QString schemaDiagnostic;addSchemaDatabaseTables(s,&out->m_tables,&schemaDiagnostic);if(!schemaDiagnostic.isEmpty()){if(d)*d=schemaDiagnostic;return {};}
    QMap<QString,LosslessDatabaseMember> parsedMembers;
    for(auto tableIt=out->m_tables.constBegin();tableIt!=out->m_tables.constEnd();++tableIt)if(tableIt->structuralEditing)
        for(const auto& sourceRow:tableIt->rows)for(const auto& field:tableIt->fields)if(field.writable)
        {
            SDatabaseChange probe;probe.tableKey=tableIt.key();probe.rowKey=sourceRow.stableKey;probe.fieldKey=field.key;PatchTarget target;if(!patchTarget(probe,tableIt.value(),&target))continue;
            if(!parsedMembers.contains(target.member))parsedMembers.insert(target.member,LosslessDatabaseMember::parse(s->rawMembers.value(target.member)));
            const auto& member=parsedMembers[target.member];const int recordIndex=member.findRecordByFieldPayload(target.table,0,stringPayload(sourceRow.sourceStableKey));if(recordIndex<0)continue;
            for(const auto& sourceField:member.fields(target.table,recordIndex))if(sourceField.id==target.field&&sourceField.occurrence==0)out->m_sourceFieldOccurrences.insert(tableIt.key()+'|'+fold(sourceRow.sourceStableKey)+'|'+field.key);
        }
    out->m_baselineTables=out->m_tables;QFile sourceFile(s->sourceArchivePath);if(sourceFile.open(QIODevice::ReadOnly))out->m_sourceArchiveBaseline=sourceFile.readAll();out->m_expectedSourceSha256=s->sourceArchiveSha256;
    return out;
}
QString DatabaseDocument::profileId()const{return m_snapshot->profileId;} QString DatabaseDocument::providerId()const{return m_snapshot->providerId;}
QString DatabaseDocument::sourceArchivePath()const{return m_snapshot->sourceArchivePath;} QString DatabaseDocument::sourceArchiveSha256()const{return m_snapshot->sourceArchiveSha256;}
QStringList DatabaseDocument::tableKeys()const{return m_tables.keys();} const SDatabaseTable* DatabaseDocument::table(const QString& k)const{auto i=m_tables.constFind(k);return i==m_tables.constEnd()?nullptr:&i.value();}
bool DatabaseDocument::isDirty()const{return !m_undo.isEmpty();} bool DatabaseDocument::canUndo()const{return !m_undo.isEmpty();} bool DatabaseDocument::canRedo()const{return !m_redo.isEmpty();}
bool DatabaseDocument::hasUnappliedChanges(QString* diagnostic)const{const QByteArray candidate=buildArchive(diagnostic);if(candidate.isEmpty())return true;QFile live(sourceArchivePath());if(!live.open(QIODevice::ReadOnly)){if(diagnostic)*diagnostic="The active database cannot be read.";return true;}return live.readAll()!=candidate;}
bool DatabaseDocument::identityAvailable(const QString&t,const QString&identity,const QString&except)const{const auto*tb=table(t);const QString wanted=fold(identity),ignored=fold(except);if(!tb||wanted.isEmpty())return false;const QString identityKey=tb->identityFieldId>=0?QString("fld.%1.0").arg(tb->identityFieldId):QString("name");for(const auto&r:tb->rows)if(!r.deleted&&fold(r.values.value(identityKey).toString())==wanted&&r.stableKey!=ignored)return false;return true;}
QString DatabaseDocument::displayIdentity(const QString&t,const QString&k)const{const auto*tb=table(t);if(!tb)return QString();const QString wanted=fold(k);for(const auto&r:tb->rows)if(r.stableKey==wanted){if(tb->identityFieldId>=0)return r.values.value(QString("fld.%1.0").arg(tb->identityFieldId)).toString();return r.values.value("name").toString();}return QString();}
bool DatabaseDocument::fieldEditable(const QString&t,const QString&r,const QString&f,QString*d)const
{
    const SDatabaseTable*tb=table(t);const SDatabaseRow*x=nullptr;if(tb)for(const auto&candidate:tb->rows)if(candidate.stableKey==fold(r)){x=&candidate;break;}
    if(!x||!tb){if(d)*d="Row is unavailable.";return false;}bool writable=false;for(const auto&s:tb->fields)writable|=s.key==f&&s.writable;if(!writable){if(d)*d="Field is unsupported or read-only.";return false;}
    if(tb->physicalTable>=0){if(x->added||x->values.contains(f))return true;if(d)*d="This optional field is absent from the source row; safe field insertion is not yet proven.";return false;}
    SDatabaseChange physical;physical.tableKey=t;physical.rowKey=r;physical.fieldKey=f;PatchTarget target;if(!patchTarget(physical,*tb,&target)){if(d)*d="This field has no verified publication mapping for this row.";return false;}
    const QString sourceName=x->sourceStableKey.isEmpty()?x->values.value("name").toString():x->sourceStableKey;if(m_sourceFieldOccurrences.contains(t+'|'+fold(sourceName)+'|'+f))return true;if(d)*d="This optional field is absent from the source row; safe field insertion is not yet proven.";return false;
}
QVector<SDatabaseChange> DatabaseDocument::changes()const{QVector<SDatabaseChange> r;for(const auto&s:m_undo)r<<s.change;return r;}
SDatabaseRow* DatabaseDocument::row(const QString&t,const QString&k){auto i=m_tables.find(t);if(i==m_tables.end())return nullptr;for(auto&r:i->rows)if(r.stableKey==fold(k))return &r;return nullptr;}
void DatabaseDocument::commit(const QMap<QString,SDatabaseTable>&b,const SDatabaseChange&c){m_undo.append({b,c});m_redo.clear();}
bool DatabaseDocument::setValue(const QString&t,const QString&r,const QString&f,const QVariant&v,QString*d)
{SDatabaseRow*x=row(t,r);const SDatabaseTable*tb=table(t);if(!fieldEditable(t,r,f,d))return false;const QString identityKey=tb->identityFieldId>=0?QString("fld.%1.0").arg(tb->identityFieldId):QString("name");if(f==identityKey){QString k=fold(v.toString());if(k.isEmpty()){if(d)*d="Stable name cannot be empty.";return false;}if(!identityAvailable(t,k,x->stableKey)){if(d)*d="Stable name collides with an existing row.";return false;}}QVariant old=x->values.value(f);if(old==v)return true;auto before=m_tables;x=row(t,r);x->values[f]=v;if(f==identityKey)x->stableKey=fold(v.toString())+(tb->physicalTable>=0?QString("#%1").arg(x->sourceRecordIndex):QString());commit(before,{t,r,f,old,v,QString("Changed %1.%2").arg(r,f)});return true;}
bool DatabaseDocument::cloneRow(const QString&t,const QString&r,const QString&n,QString*d){SDatabaseRow*s=row(t,r);auto tb=m_tables.find(t);QString k=fold(n);if(!s||tb==m_tables.end()||!tb->structuralEditing){if(d)*d="Row cloning is unavailable for this preservation-only table.";return false;}if(k.isEmpty()||!identityAvailable(t,n)){if(d)*d="New stable name is empty or already exists.";return false;}SDatabaseRow c=*s;auto before=m_tables;tb=m_tables.find(t);c.sourceStableKey=s->sourceStableKey.isEmpty()?s->stableKey:s->sourceStableKey;c.stableKey=tb->physicalTable>=0?k+QString("#new%1").arg(tb->rows.size()):k;const QString identityKey=tb->identityFieldId>=0?QString("fld.%1.0").arg(tb->identityFieldId):QString("name");c.values[identityKey]=n;c.added=true;c.deleted=false;tb->rows<<c;commit(before,{t,c.stableKey,{}, {},n,QString("Cloned %1 as %2").arg(r,n)});return true;}
bool DatabaseDocument::deleteRow(const QString&t,const QString&r,QString*d){auto tb=m_tables.find(t);auto*x=row(t,r);if(tb==m_tables.end()||!tb->structuralEditing){if(d)*d="Row deletion is unavailable for this preservation-only table.";return false;}if(!x||x->deleted){if(d)*d="Row is unavailable.";return false;}auto before=m_tables;x=row(t,r);x->deleted=true;commit(before,{t,r,{},false,true,QString("Deleted %1").arg(r)});return true;}
bool DatabaseDocument::undo(){if(m_undo.isEmpty())return false;auto s=m_undo.takeLast();m_redo.append({m_tables,s.change});m_tables=s.tables;return true;} bool DatabaseDocument::redo(){if(m_redo.isEmpty())return false;auto s=m_redo.takeLast();m_undo.append({m_tables,s.change});m_tables=s.tables;return true;}
QVector<SDatabaseValidationFinding> DatabaseDocument::validate()const{QVector<SDatabaseValidationFinding>o;for(const auto&tb:m_tables){if(!tb.structuralEditing)continue;QMap<QString,int>ids;for(const auto&r:tb.rows){if(r.deleted)continue;const QString identityKey=tb.identityFieldId>=0?QString("fld.%1.0").arg(tb.identityFieldId):QString("name");QString n=fold(r.values.value(identityKey).toString());const bool identified=tb.identityFieldId>=0||tb.key=="raceModels"||tb.key=="monsters"||tb.key=="items";if(identified&&(n.isEmpty()||++ids[n]>1))o.append({SDatabaseValidationFinding::Severity::Error,"database.identity.invalid",tb.key,r.stableKey,"Stable name is empty or duplicated.",true});for(const auto&f:tb.fields){if(f.referenceTable.isEmpty())continue;QString ref=fold(r.values.value(f.key).toString());const auto*target=table(f.referenceTable);bool found=ref.isEmpty();if(target)for(const auto&c:target->rows)found|=!c.deleted&&c.stableKey==ref;if(!found)o.append({SDatabaseValidationFinding::Severity::Error,"database.reference.missing",tb.key,r.stableKey,QString("%1 references missing %2 row '%3'.").arg(f.label,f.referenceTable,ref),true});}}}if(isDirty())o.append({SDatabaseValidationFinding::Severity::Warning,"database.impact.opaque",{}, {},"MOB, texts.res, and descriptor-unknown references require external impact review before publication.",false});return o;}
QJsonObject DatabaseDocument::pendingChangeManifest()const{QJsonObject o;o["format"]="ei-mapper-database-change-set-v1";o["providerId"]=providerId();o["profileId"]=profileId();o["sourceArchive"]=sourceArchivePath();o["sourceSha256"]=sourceArchiveSha256();QString publicationDiagnostic;o["publicationSupported"]=!buildArchive(&publicationDiagnostic).isEmpty();o["publicationDiagnostic"]=publicationDiagnostic;QJsonArray a;for(const auto&c:changes()){QJsonObject x;x["table"]=c.tableKey;x["row"]=c.rowKey;x["field"]=c.fieldKey;x["before"]=jv(c.before);x["after"]=jv(c.after);x["description"]=c.description;a<<x;}o["changes"]=a;QJsonArray p;for(auto i=m_snapshot->rawMembers.cbegin();i!=m_snapshot->rawMembers.cend();++i)p<<i.key();o["preservedRawMembers"]=p;return o;}
bool DatabaseDocument::sourceIsCurrent(QString*d)const{QFile f(sourceArchivePath());if(!f.open(QIODevice::ReadOnly)){if(d)*d="Source archive is no longer readable.";return false;}QString h=QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex());if(h.compare(m_expectedSourceSha256,Qt::CaseInsensitive)!=0){if(d)*d="Source archive changed outside this database editor session.";return false;}return true;}
QByteArray DatabaseDocument::buildArchive(QString* diagnostic)const
{
    QByteArray archiveBytes=m_sourceArchiveBaseline;if(archiveBytes.isEmpty()){if(diagnostic)*diagnostic="Source archive baseline is unavailable.";return {};}
    if(!sourceIsCurrent(diagnostic))return {};
    // The immutable baseline is the authoritative no-op representation. This
    // is the byte-identity gate and avoids ever normalizing an untouched RES.
    // Workbook replacement validation opts into candidate serialization below.
    if(!isDirty()&&!m_buildingReplacementCandidate)return archiveBytes;
    bool anyStateChange=false;
    for(auto tableIt=m_tables.constBegin();tableIt!=m_tables.constEnd()&&!anyStateChange;++tableIt)
    {
        const SDatabaseTable baseline=m_baselineTables.value(tableIt.key());
        for(const auto& row:tableIt->rows)
        {
            anyStateChange|=(row.added&&!row.deleted)||(!row.added&&row.deleted);
            const QString baselineKey=fold(row.sourceStableKey.isEmpty()?row.stableKey:row.sourceStableKey);const SDatabaseRow* base=nullptr;if(row.sourceRecordIndex>=0&&row.sourceRecordIndex<baseline.rows.size())base=&baseline.rows.at(row.sourceRecordIndex);else for(const auto& candidate:baseline.rows)if(candidate.stableKey==baselineKey){base=&candidate;break;}
            if(!row.deleted&&base)for(const auto& field:tableIt->fields)anyStateChange|=field.writable&&!sameValue(base->values.value(field.key),row.values.value(field.key));
        }
    }
    if(!anyStateChange)return archiveBytes;
    QMap<QString,LosslessDatabaseMember> members;
    for(auto tableIt=m_tables.constBegin();tableIt!=m_tables.constEnd();++tableIt)
    {
        const SDatabaseTable& table=tableIt.value();const SDatabaseTable baseline=m_baselineTables.value(table.key);
        bool tableChanged=false;for(const auto& row:table.rows)tableChanged|=(row.added&&!row.deleted)||(!row.added&&row.deleted);for(const auto& row:table.rows)if(!row.deleted){const SDatabaseRow* base=nullptr;const QString baselineKey=row.sourceStableKey.isEmpty()?row.stableKey:fold(row.sourceStableKey);if(row.sourceRecordIndex>=0&&row.sourceRecordIndex<baseline.rows.size())base=&baseline.rows.at(row.sourceRecordIndex);else for(const auto& candidate:baseline.rows)if(candidate.stableKey==baselineKey){base=&candidate;break;}if(base)for(const auto& field:table.fields)tableChanged|=field.writable&&!sameValue(base->values.value(field.key),row.values.value(field.key));}
        if(!tableChanged)
            continue;
        QString parseDiagnostic;auto member=members.contains(table.sourceMember)
            ?members.value(table.sourceMember)
            :LosslessDatabaseMember::parse(m_snapshot->rawMembers.value(table.sourceMember),&parseDiagnostic);
        if(!member.valid()){if(diagnostic)*diagnostic=parseDiagnostic;return {};}
        if(table.physicalTable>=0)
        {
            const auto& schema=DatabaseSchema::instance();auto baselineRowIndex=[&](const SDatabaseRow& row)->int{if(row.sourceRecordIndex>=0&&row.sourceRecordIndex<baseline.rows.size())return row.sourceRecordIndex;const QString sourceKey=fold(row.sourceStableKey.isEmpty()?row.stableKey:row.sourceStableKey);for(int i=0;i<baseline.rows.size();++i)if(baseline.rows.at(i).stableKey==sourceKey)return i;return -1;};
            auto publishFields=[&](int record,const SDatabaseRow& row,const SDatabaseRow* base)->bool
            {
                const auto existing=member.fields(table.physicalTable,record);
                for(const auto& field:table.fields)
                {
                    if(!field.writable)continue;const QVariant before=base?base->values.value(field.key):QVariant();const QVariant after=row.values.value(field.key);if(base&&sameValue(before,after))continue;if(!row.values.contains(field.key))continue;
                    QByteArray sourcePayload;bool occurrence=false;for(const auto& stored:existing)if(stored.id==field.storageId&&stored.occurrence==field.storageOccurrence){sourcePayload=stored.payload;occurrence=true;break;}if(!occurrence){if(diagnostic)*diagnostic=QString("Field %1 is absent from the source record.").arg(field.key);return false;}
                    SDatabaseStorageField fallback;fallback.id=field.storageId;fallback.type=EDatabaseStorageType::Hex;const SDatabaseStorageField* storage=schema.field(table.schemaFamily,table.blockId,field.storageId);if(!storage)storage=&fallback;QString error;const QByteArray payload=DatabaseValueCodec::encode(after,sourcePayload,*storage,schema,table.schemaFamily,&error);if(!error.isEmpty()){if(diagnostic)*diagnostic=QString("%1: %2").arg(field.label,error);return false;}if(!member.replaceFieldPayload(table.physicalTable,record,field.storageId,field.storageOccurrence,payload,&error)){if(diagnostic)*diagnostic=error;return false;}
                }
                return true;
            };
            for(const auto& row:table.rows)if(!row.deleted&&!row.added){const int index=baselineRowIndex(row);if(index<0){if(diagnostic)*diagnostic="Stable source record is missing.";return {};}const SDatabaseRow& base=baseline.rows.at(index);if(!publishFields(index,row,&base))return {};}
            for(const auto& row:table.rows)if(row.added&&!row.deleted){const int source=baselineRowIndex(row);if(source<0){if(diagnostic)*diagnostic="Clone source record is missing.";return {};}QString error;if(!member.cloneRecord(table.physicalTable,source,&error)){if(diagnostic)*diagnostic=error;return {};}if(!publishFields(member.recordCount(table.physicalTable)-1,row,nullptr))return {};}
            QVector<int> deleted;for(const auto& row:table.rows)if(row.deleted&&!row.added){const int index=baselineRowIndex(row);if(index>=0)deleted.append(index);}std::sort(deleted.begin(),deleted.end(),std::greater<int>());for(int index:deleted){QString error;if(!member.removeRecord(table.physicalTable,index,&error)){if(diagnostic)*diagnostic=error;return {};}}
            members.insert(table.sourceMember,member);continue;
        }
        for(const auto& row:table.rows)if(row.added&&!row.deleted){SDatabaseChange probe;probe.tableKey=table.key;probe.rowKey=row.stableKey;probe.fieldKey="name";PatchTarget structural;if(!patchTarget(probe,table,&structural)){if(diagnostic)*diagnostic="Clone table mapping is unavailable.";return {};}QString e;if(!member.cloneRecordByFieldPayload(structural.table,0,stringPayload(row.sourceStableKey),stringPayload(row.values.value("name")),&e)){if(diagnostic)*diagnostic=e;return {};}}
        for(const auto& row:table.rows)if(row.deleted&&!row.added){SDatabaseChange probe;probe.tableKey=table.key;probe.rowKey=row.stableKey;probe.fieldKey="name";PatchTarget structural;if(!patchTarget(probe,table,&structural)){if(diagnostic)*diagnostic="Delete table mapping is unavailable.";return {};}const QString sourceName=row.sourceStableKey.isEmpty()?row.values.value("name").toString():row.sourceStableKey;QString e;if(!member.removeRecordByFieldPayload(structural.table,0,stringPayload(sourceName),&e)){if(diagnostic)*diagnostic=e;return {};}}
        // Publish identity last so every other changed field still locates an
        // existing record by its immutable source identity.
        QVector<SDatabaseFieldSchema> publicationFields=table.fields;std::stable_sort(publicationFields.begin(),publicationFields.end(),[](const SDatabaseFieldSchema&a,const SDatabaseFieldSchema&b){return a.key!="name"&&b.key=="name";});
        for(const auto& row:table.rows)if(!row.deleted)for(const auto& field:publicationFields)if(field.writable)
        {
            const SDatabaseRow* base=nullptr;const QString baselineKey=fold(row.sourceStableKey.isEmpty()?row.stableKey:row.sourceStableKey);for(const auto& candidate:baseline.rows)if(candidate.stableKey==baselineKey){base=&candidate;break;}
            if(base&&base->values.value(field.key)==row.values.value(field.key))continue;
            SDatabaseChange change;change.tableKey=table.key;change.rowKey=row.stableKey;change.fieldKey=field.key;change.after=row.values.value(field.key);PatchTarget target;if(!patchTarget(change,table,&target)){if(diagnostic)*diagnostic=QString("Field %1 is visible but not yet publishable.").arg(field.key);return {};}
            const QString lookupName=row.added?row.values.value("name").toString():(row.sourceStableKey.isEmpty()?row.values.value("name").toString():row.sourceStableKey);int record=member.findRecordByFieldPayload(target.table,0,stringPayload(lookupName));if(record<0){if(diagnostic)*diagnostic="Stable row identity is missing or ambiguous.";return {};}
            QByteArray sourcePayload;
            if(target.kind == PatchTarget::NestedStrings)
                for(const auto& existing : member.fields(target.table, record))
                    if(existing.id == target.field && existing.occurrence == 0)
                    {
                        sourcePayload = existing.payload;
                        break;
                    }
            const QByteArray payload=target.kind==PatchTarget::String?stringPayload(change.after):(target.kind==PatchTarget::Integer?intPayload(change.after):(target.kind==PatchTarget::Float?floatPayload(change.after):nestedStringPayload(change.after,sourcePayload)));QString e;if(!member.replaceFieldPayload(target.table,record,target.field,0,payload,&e)){if(diagnostic)*diagnostic=e;return {};}
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
    const auto before=m_tables;
    if(tables.keys()!=before.keys()){if(diagnostic)*diagnostic="The workbook candidate changed the database table set.";return false;}
    for(auto tableIt=tables.constBegin();tableIt!=tables.constEnd();++tableIt)
    {
        const auto originalIt=before.constFind(tableIt.key());
        if(originalIt==before.constEnd()||tableIt->fields.size()!=originalIt->fields.size()){if(diagnostic)*diagnostic="The workbook candidate changed a table schema.";return false;}
        for(int fieldIndex=0;fieldIndex<tableIt->fields.size();++fieldIndex)
            if(tableIt->fields.at(fieldIndex).key!=originalIt->fields.at(fieldIndex).key
               ||tableIt->fields.at(fieldIndex).writable!=originalIt->fields.at(fieldIndex).writable)
            {if(diagnostic)*diagnostic="The workbook candidate changed a field schema.";return false;}
        for(const auto& candidateRow:tableIt->rows)
        {
            const SDatabaseRow* originalRow=nullptr;
            for(const auto& row:originalIt->rows)if(row.stableKey==candidateRow.stableKey){originalRow=&row;break;}
            if(!originalRow)continue;
            for(const auto& field:tableIt->fields)
                if(!field.writable&&candidateRow.values.value(field.key)!=originalRow->values.value(field.key))
                {if(diagnostic)*diagnostic=QString("Read-only field %1.%2 was changed from '%3' to '%4'.").arg(tableIt.key(),field.key,originalRow->values.value(field.key).toString(),candidateRow.values.value(field.key).toString());return false;}
        }
    }
    const auto baselineFindings=validate();QSet<QString> baselineErrors;for(const auto& finding:baselineFindings)if(finding.severity==SDatabaseValidationFinding::Severity::Error)baselineErrors.insert(finding.code+'|'+finding.tableKey+'|'+finding.rowKey+'|'+finding.message);m_tables=tables;QSet<QString> candidateErrors;for(const auto& finding:validate())if(finding.severity==SDatabaseValidationFinding::Severity::Error)candidateErrors.insert(finding.code+'|'+finding.tableKey+'|'+finding.rowKey+'|'+finding.message);for(const QString& error:candidateErrors)if(!baselineErrors.contains(error)){m_tables=before;if(diagnostic)*diagnostic=error.section('|',3);return false;}
    QString publicationDiagnostic;m_buildingReplacementCandidate=true;const QByteArray candidate=buildArchive(&publicationDiagnostic);m_buildingReplacementCandidate=false;if(candidate.isEmpty()){m_tables=before;if(diagnostic)*diagnostic=publicationDiagnostic;return false;}
    commit(before,{"workbook",{}, {}, {}, {},description});return true;
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
