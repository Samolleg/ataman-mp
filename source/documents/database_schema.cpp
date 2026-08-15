#include "database_schema.h"

#include <QFile>
#include <QRegExp>
#include <QTextStream>

namespace {
QString canonicalFamily(const QString& value){return value.trimmed().toLower();}
QString memberForFamily(const QString& family)
{
    const QMap<QString,QString> names={{"items","items.idb"},{"levers","levers.ldb"},
        {"perks","perks.pdb"},{"prints","prints.db"},{"spells","spells.sdb"},
        {"units","units.udb"},{"acks","acks.db"},{"quests","quests.qdb"}};
    return names.value(canonicalFamily(family));
}
QByteArray resource(const QString& name,QString* diagnostic)
{
    QFile file(":/database_schema/"+name);
    if(!file.open(QIODevice::ReadOnly)){if(diagnostic)*diagnostic=file.errorString();return {};}
    return file.readAll();
}
bool descriptor(const QString& text,ei::EDatabaseStorageType* type,int* modifier,QByteArray* data)
{
    QString name=text.trimmed(),argument;const int open=name.indexOf('(');
    if(open>=0&&name.endsWith(')')){argument=name.mid(open+1,name.size()-open-2);name=name.left(open);}
    static const QMap<QString,ei::EDatabaseStorageType> values={
        {"Hex",ei::EDatabaseStorageType::Hex},{"String",ei::EDatabaseStorageType::String},
        {"UnsignedLong",ei::EDatabaseStorageType::UnsignedLong},{"SignedLong",ei::EDatabaseStorageType::SignedLong},
        {"Float",ei::EDatabaseStorageType::Float},{"Byte",ei::EDatabaseStorageType::Byte},
        {"UnsignedLongList",ei::EDatabaseStorageType::UnsignedLongList},{"FloatList",ei::EDatabaseStorageType::FloatList},
        {"IndexedFloat",ei::EDatabaseStorageType::Float},{"IndexedFloatList",ei::EDatabaseStorageType::IndexedFloatList},
        {"ByteList",ei::EDatabaseStorageType::ByteList},{"StringList",ei::EDatabaseStorageType::StringList},
        {"MinfixedStringList",ei::EDatabaseStorageType::FixedStringList},{"BitList",ei::EDatabaseStorageType::BitList},
        {"TypeList",ei::EDatabaseStorageType::TypeList},{"CapsulatedTypeList",ei::EDatabaseStorageType::CapsulatedTypeList},
        {"AcksUniqueType",ei::EDatabaseStorageType::AcksUniqueType},{"FixedString",ei::EDatabaseStorageType::FixedString}};
    if(!values.contains(name))return false;*type=values.value(name);*modifier=0;data->clear();
    if(!argument.isEmpty()){const QStringList parts=argument.split(',');*modifier=parts.value(0).trimmed().toInt();if(parts.size()>1)*data=QByteArray::fromHex(parts.value(1).trimmed().toLatin1());}
    return true;
}
}

namespace ei {
const DatabaseSchema& DatabaseSchema::instance(){static const DatabaseSchema schema;return schema;}
DatabaseSchema::DatabaseSchema()
{
    QString issue;const QByteArray typesData=resource("dbtypes.txt",&issue),headersData=resource("dbheaders.txt",&issue),blocksData=resource("dbblocks.txt",&issue);
    if(typesData.isEmpty()||headersData.isEmpty()||blocksData.isEmpty()){m_diagnostic="Database schema resources are unavailable: "+issue;return;}
    QString family;
    for(const QByteArray& raw:typesData.split('\n'))
    {
        const QString line=QString::fromUtf8(raw).trimmed();if(line.isEmpty()||line.startsWith('#'))continue;
        if(line.startsWith('[')&&line.endsWith(']')){family=canonicalFamily(line.mid(1,line.size()-2));continue;}
        const QStringList parts=line.split(QRegExp("\\s+"),QString::SkipEmptyParts);if(parts.size()<3)continue;
        SDatabaseStorageField field;field.id=parts.at(1).toInt();if(!descriptor(parts.at(2),&field.type,&field.modifier,&field.typeData)){m_diagnostic="Unknown database type descriptor: "+parts.at(2);return;}m_types[family][parts.at(0).toInt()][field.id]=field;
    }
    family.clear();
    for(const QByteArray& raw:headersData.split('\n'))
    {
        const QString line=QString::fromUtf8(raw).trimmed();if(line.isEmpty()||line.startsWith('#'))continue;
        if(line.startsWith('[')&&line.endsWith(']')){family=canonicalFamily(line.mid(1,line.size()-2));continue;}
        const QStringList parts=line.split('\t');if(parts.size()<5)continue;SDatabaseStorageHeader header;header.id=parts.at(1).trimmed().toInt();header.index=parts.at(2).trimmed().toInt();header.width=parts.at(3).trimmed().toInt();header.name=parts.at(4).trimmed();header.description=parts.value(5).trimmed();m_headers[family][parts.at(0).trimmed().toInt()].append(header);
    }
    QMap<QString,int> indexes;
    for(const QByteArray& raw:blocksData.split('\n'))
    {
        const QString line=QString::fromUtf8(raw).trimmed();if(line.isEmpty()||line.startsWith('#'))continue;
        const QStringList parts=line.split('\t');if(parts.size()<3)continue;SDatabaseStorageTable table;table.family=canonicalFamily(parts.at(0));table.memberName=memberForFamily(table.family);table.blockId=parts.at(1).toInt();if(table.blockId>=20)continue;table.physicalIndex=indexes[table.family]++;table.name=parts.at(2).trimmed();table.title=parts.value(4).trimmed();if(table.title.isEmpty())table.title=table.name;table.key=table.family+"."+table.name.toLower();for(const auto& field:m_types.value(table.family).value(table.blockId))table.fields.append(field);table.headers=m_headers.value(table.family).value(table.blockId);m_tables.append(table);
    }
    if(m_tables.size()!=26)m_diagnostic=QString("Expected 26 public database tables, parsed %1.").arg(m_tables.size());
}
QVector<SDatabaseStorageTable> DatabaseSchema::tables()const{return m_tables;}
const SDatabaseStorageTable* DatabaseSchema::tableForMember(const QString& member,int index)const{for(const auto& table:m_tables)if(table.memberName.compare(member,Qt::CaseInsensitive)==0&&table.physicalIndex==index)return &table;return nullptr;}
const SDatabaseStorageField* DatabaseSchema::field(const QString& family,int block,int id)const{auto fi=m_types.constFind(canonicalFamily(family));if(fi==m_types.constEnd())return nullptr;auto bi=fi->constFind(block);if(bi==fi->constEnd())return nullptr;auto ii=bi->constFind(id);return ii==bi->constEnd()?nullptr:&ii.value();}
QVector<SDatabaseStorageHeader> DatabaseSchema::headers(const QString& family,int block,int id)const{QVector<SDatabaseStorageHeader> result;for(const auto& h:m_headers.value(canonicalFamily(family)).value(block))if(h.id==id)result.append(h);return result;}
QString DatabaseSchema::diagnostic()const{return m_diagnostic;}
}
