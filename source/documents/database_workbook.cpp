#include "database_workbook.h"
#include "database_schema.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>
#include <QtGui/private/qzipreader_p.h>
#include <QtGui/private/qzipwriter_p.h>

namespace {
QString xml(const QString& value){QString clean=value;for(int i=0;i<clean.size();++i){const ushort u=clean.at(i).unicode();if((u<0x20&&u!='\t'&&u!='\n'&&u!='\r')||u==0xfffe||u==0xffff)clean[i]=QChar(0xfffd);}return clean.toHtmlEscaped();}
QString column(int index){QString out;for(++index;index;index=(index-1)/26)out.prepend(QChar('A'+(index-1)%26));return out;}
struct WorkbookColumn{int field{-1},index{0};QString label;ei::EDatabaseStorageType storageType{ei::EDatabaseStorageType::Hex};ei::EDatabaseFieldType displayType{ei::EDatabaseFieldType::Text};};
QString cell(const QString& ref,const QVariant& value,ei::EDatabaseFieldType type,QStringList* strings)
{
    if((type==ei::EDatabaseFieldType::Integer||type==ei::EDatabaseFieldType::Decimal)&&value.isValid())
        return QString("<c r=\"%1\"><v>%2</v></c>").arg(ref,xml(value.toString()));
    const QString text=value.type()==QVariant::StringList?value.toStringList().join(","):value.toString();int index=strings->indexOf(text);if(index<0){strings->append(text);index=strings->size()-1;}return QString("<c r=\"%1\" t=\"s\"><v>%2</v></c>").arg(ref).arg(index);
}
QString sheetName(const ei::SDatabaseTable& table){for(const auto& storage:ei::DatabaseSchema::instance().tables())if(storage.memberName==table.sourceMember&&storage.physicalIndex==table.physicalTable)return storage.name.left(31);return table.label.left(31);}
QVector<WorkbookColumn> columns(const ei::SDatabaseTable& table)
{
    QVector<WorkbookColumn> result;const auto& schema=ei::DatabaseSchema::instance();for(const auto& field:table.fields){const auto* storage=schema.field(table.schemaFamily,table.blockId,field.storageId);if(!storage)continue;auto headers=schema.headers(table.schemaFamily,table.blockId,field.storageId);if(headers.isEmpty()){ei::SDatabaseStorageHeader h;h.id=field.storageId;h.name=field.label;headers.append(h);}for(const auto& h:headers){WorkbookColumn c;c.field=field.storageId;c.index=h.index;c.label=h.name;c.storageType=storage->type;c.displayType=(storage->type==ei::EDatabaseStorageType::SignedLong||storage->type==ei::EDatabaseStorageType::UnsignedLong||storage->type==ei::EDatabaseStorageType::Byte)?ei::EDatabaseFieldType::Integer:(storage->type==ei::EDatabaseStorageType::Float?ei::EDatabaseFieldType::Decimal:ei::EDatabaseFieldType::Text);result.append(c);}}return result;
}
QJsonArray array(const QVariant& value){QJsonParseError e;const auto d=QJsonDocument::fromJson(value.toString().toUtf8(),&e);return e.error==QJsonParseError::NoError&&d.isArray()?d.array():QJsonArray();}
QVariant compatibilityValue(const QVariant& value,const WorkbookColumn& c)
{
    if(c.storageType==ei::EDatabaseStorageType::StringList||c.storageType==ei::EDatabaseStorageType::FixedStringList){QStringList out;for(const auto& v:array(value))out<<v.toString().replace(',',';');return out.join(',');}
    const QJsonArray values=array(value);if(c.storageType==ei::EDatabaseStorageType::FloatList||c.storageType==ei::EDatabaseStorageType::UnsignedLongList||c.storageType==ei::EDatabaseStorageType::ByteList||c.storageType==ei::EDatabaseStorageType::BitList)return c.index<values.size()?values.at(c.index).toVariant():QVariant();
    if(c.storageType==ei::EDatabaseStorageType::IndexedFloatList||c.storageType==ei::EDatabaseStorageType::TypeList)for(const auto& v:values){const auto o=v.toObject();if(o.value("id").toInt()==c.index)return o.value("value").toVariant();}
    if(c.storageType==ei::EDatabaseStorageType::AcksUniqueType){QStringList records;for(const auto& v:values){QStringList fields;for(const auto& f:v.toObject().value("fields").toArray()){const auto o=f.toObject();fields<<QString("%1=%2").arg(o.value("id").toInt()).arg(o.value("value").toVariant().toString());}records<<"{"+fields.join("##")+"}";}return records.join(QString());}
    if(c.storageType==ei::EDatabaseStorageType::CapsulatedTypeList)for(const auto& v:values){const auto outer=v.toObject();for(const auto& f:outer.value("fields").toArray()){const auto o=f.toObject();if(o.value("id").toInt()==c.index)return o.value("value").toVariant();}}
    return c.index==0?value:QVariant();
}
bool compatibilityEqual(const QString& workbookValue,const QVariant& baseline,const WorkbookColumn& c)
{
    if(c.displayType==ei::EDatabaseFieldType::Integer){bool leftOk=false,rightOk=false;const qlonglong left=workbookValue.toLongLong(&leftOk),right=baseline.toString().toLongLong(&rightOk);return leftOk&&rightOk&&left==right;}
    if(c.displayType==ei::EDatabaseFieldType::Decimal){bool leftOk=false,rightOk=false;const double left=workbookValue.toDouble(&leftOk),right=baseline.toString().toDouble(&rightOk);return leftOk&&rightOk&&qAbs(left-right)<=5e-7;}
    return workbookValue==baseline.toString();
}
QByteArray sheetXml(const ei::SDatabaseTable& table,QStringList* strings)
{
    const auto cols=columns(table);const int extra=2;int outputRow=3;for(const auto& row:table.rows)if(!row.deleted)++outputRow;const QString last=column(cols.size()+extra-1)+QString::number(qMax(2,outputRow-1));QString x="<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><dimension ref=\"A1:"+last+"\"/><sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"2\" topLeftCell=\"A3\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews><sheetData><row r=\"1\">";
    for(int c=0;c<cols.size();++c)x+=cell(column(c)+"1",cols[c].label,ei::EDatabaseFieldType::Text,strings);x+=cell(column(cols.size())+"1","_sourceRecord",ei::EDatabaseFieldType::Text,strings)+cell(column(cols.size()+1)+"1","_sourceName",ei::EDatabaseFieldType::Text,strings)+"</row><row r=\"2\">";
    for(int c=0;c<cols.size();++c)x+=cell(column(c)+"2",QString("FLD%1-%2").arg(cols[c].field).arg(cols[c].index),ei::EDatabaseFieldType::Text,strings);x+=cell(column(cols.size())+"2","_sourceRecord",ei::EDatabaseFieldType::Text,strings)+cell(column(cols.size()+1)+"2","_sourceName",ei::EDatabaseFieldType::Text,strings)+"</row>";
    outputRow=3;for(const auto& row:table.rows)if(!row.deleted){x+=QString("<row r=\"%1\">").arg(outputRow);for(int c=0;c<cols.size();++c){const QString key=QString("fld.%1.0").arg(cols[c].field);if(row.values.contains(key)){const QVariant v=compatibilityValue(row.values.value(key),cols[c]);if(v.isValid())x+=cell(column(c)+QString::number(outputRow),v,cols[c].displayType,strings);}}x+=cell(column(cols.size())+QString::number(outputRow),row.sourceRecordIndex,ei::EDatabaseFieldType::Integer,strings)+cell(column(cols.size()+1)+QString::number(outputRow),row.sourceStableKey,ei::EDatabaseFieldType::Text,strings)+"</row>";++outputRow;}x+="</sheetData><autoFilter ref=\"A2:"+last+"\"/></worksheet>";return x.toUtf8();
}
QByteArray sharedStrings(const QStringList& strings){QString x=QString("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"%1\" uniqueCount=\"%1\">").arg(strings.size());for(const QString& s:strings)x+="<si><t xml:space=\"preserve\">"+xml(s)+"</t></si>";return (x+"</sst>").toUtf8();}
QMap<QString,QString> cells(const QByteArray& bytes,const QStringList& strings,QString* diagnostic)
{
    QMap<QString,QString> out;QXmlStreamReader r(bytes);QString ref,value,type;bool inCell=false;while(!r.atEnd()){r.readNext();if(r.isStartElement()&&r.name()=="c"){ref=r.attributes().value("r").toString();type=r.attributes().value("t").toString();value.clear();inCell=true;}else if(inCell&&r.isStartElement()&&(r.name()=="v"||r.name()=="t"))value+=r.readElementText();else if(r.isEndElement()&&r.name()=="c"){out[ref]=type=="s"?strings.value(value.toInt()):value;inCell=false;}}
    if(r.hasError()&&diagnostic)*diagnostic=r.errorString();return out;
}
QStringList readSharedStrings(const QByteArray& bytes){QStringList out;QXmlStreamReader r(bytes);while(!r.atEnd()){r.readNext();if(r.isStartElement()&&r.name()=="t")out<<r.readElementText();}return out;}
QMap<QString,QString> workbookSheets(const QByteArray& bytes)
{
    QMap<QString,QString> out;QXmlStreamReader r(bytes);while(!r.atEnd()){r.readNext();if(r.isStartElement()&&r.name()=="sheet"){const auto a=r.attributes();const QString name=a.value("name").toString(),rid=a.value("r:id").toString();if(!name.isEmpty()&&!rid.isEmpty())out[name.toLower()]=rid;}}return out;
}
QMap<QString,QString> workbookRelationships(const QByteArray& bytes)
{
    QMap<QString,QString> out;QXmlStreamReader r(bytes);while(!r.atEnd()){r.readNext();if(r.isStartElement()&&r.name()=="Relationship"){const auto a=r.attributes();QString target=a.value("Target").toString();if(target.startsWith('/'))target=target.mid(1);else target="xl/"+target;out[a.value("Id").toString()]=target;}}return out;
}
QString worksheetForTable(const ei::SDatabaseTable& table,const QMap<QString,QString>& sheets,const QMap<QString,QString>& relationships)
{
    const QString rid=sheets.value(sheetName(table).toLower());return relationships.value(rid);
}
QVariant compatibilityEditedValue(const QVariant& original,const ei::SDatabaseStorageField& storage,const QVector<QPair<int,QString>>& edits)
{
    if(edits.isEmpty())return original;
    if(storage.type==ei::EDatabaseStorageType::StringList||storage.type==ei::EDatabaseStorageType::FixedStringList){QStringList values;for(const QString& part:edits.first().second.split(',',QString::SkipEmptyParts))values<<part.trimmed();return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(values)).toJson(QJsonDocument::Compact));}
    if(storage.type==ei::EDatabaseStorageType::Hex||storage.type==ei::EDatabaseStorageType::String||storage.type==ei::EDatabaseStorageType::FixedString)return edits.first().second;
    if(storage.type==ei::EDatabaseStorageType::SignedLong||storage.type==ei::EDatabaseStorageType::UnsignedLong||storage.type==ei::EDatabaseStorageType::Byte)return edits.first().second.toLongLong();
    if(storage.type==ei::EDatabaseStorageType::Float)return edits.first().second.toDouble();
    QJsonArray values=array(original);
    if(storage.type==ei::EDatabaseStorageType::FloatList||storage.type==ei::EDatabaseStorageType::UnsignedLongList||storage.type==ei::EDatabaseStorageType::ByteList||storage.type==ei::EDatabaseStorageType::BitList){for(const auto& edit:edits){while(values.size()<=edit.first)values.append(QJsonValue());if(storage.type==ei::EDatabaseStorageType::BitList)values[edit.first]=(edit.second.toInt()!=0);else values[edit.first]=edit.second.toDouble();}return QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));}
    if(storage.type==ei::EDatabaseStorageType::IndexedFloatList||storage.type==ei::EDatabaseStorageType::TypeList){for(const auto& edit:edits){bool found=false;for(int i=0;i<values.size();++i){QJsonObject o=values[i].toObject();if(o.value("id").toInt()!=edit.first)continue;o["value"]=edit.second.toDouble();values[i]=o;found=true;break;}if(!found){QJsonObject o;o["id"]=edit.first;o["value"]=edit.second.toDouble();values.append(o);}}return QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));}
    return original;
}
QStringList physicalKeys(const ei::DatabaseDocument& document)
{
    QStringList keys;for(const QString& key:document.tableKeys()){const auto* table=document.table(key);if(table&&table->physicalTable>=0)keys.append(key);}return keys;
}
QByteArray manifest(const ei::DatabaseDocument& document,const QStringList& keys)
{
    QString text="EI Mapper Database Workbook v2\n"+document.profileId()+"\n"+document.providerId()+"\n"+document.sourceArchiveSha256()+"\n";for(const QString& key:keys)text+=key+'\n';return text.toUtf8();
}
}

namespace ei {
QStringList workbookTableKeys(const DatabaseDocument& document){return physicalKeys(document);}
bool DatabaseWorkbook::exportXlsx(const DatabaseDocument& document,const QString& path,QString* diagnostic)
{
    const QStringList keys=physicalKeys(document);if(keys.isEmpty()){if(diagnostic)*diagnostic="No schema-described database tables are available.";return false;}
    QBuffer package;package.open(QIODevice::WriteOnly);QZipWriter zip(&package);zip.setCompressionPolicy(QZipWriter::AutoCompress);
    QString types="<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Default Extension=\"res\" ContentType=\"application/octet-stream\"/><Default Extension=\"txt\" ContentType=\"text/plain\"/><Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>";
    QString workbook="<?xml version=\"1.0\" encoding=\"UTF-8\"?><workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>";
    QString rels="<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    QSet<QString> names;QStringList strings;
    for(int i=0;i<keys.size();++i){const auto* table=document.table(keys[i]);QString name=sheetName(*table);if(names.contains(name)){if(diagnostic)*diagnostic="Duplicate XLSX sheet title: "+name;return false;}names.insert(name);types+=QString("<Override PartName=\"/xl/worksheets/sheet%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>").arg(i+1);workbook+=QString("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>").arg(xml(name)).arg(i+1);rels+=QString("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%1.xml\"/>").arg(i+1);zip.addFile(QString("xl/worksheets/sheet%1.xml").arg(i+1),sheetXml(*table,&strings));}
    types+="<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/></Types>";workbook+="</sheets></workbook>";rels+=QString("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/></Relationships>").arg(keys.size()+1);zip.addFile("[Content_Types].xml",types.toUtf8());zip.addFile("_rels/.rels","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>");zip.addFile("xl/workbook.xml",workbook.toUtf8());zip.addFile("xl/_rels/workbook.xml.rels",rels.toUtf8());zip.addFile("xl/sharedStrings.xml",sharedStrings(strings));zip.addFile("ei-mapper/manifest.txt",manifest(document,keys));QFile source(document.sourceArchivePath());if(source.open(QIODevice::ReadOnly))zip.addFile("ei-mapper/source.res",source.readAll());zip.close();
    if(zip.status()!=QZipWriter::NoError){if(diagnostic)*diagnostic="Could not create the XLSX package.";return false;}QSaveFile staged(path);if(!staged.open(QIODevice::WriteOnly)){if(diagnostic)*diagnostic=staged.errorString();return false;}if(staged.write(package.data())!=package.data().size()){if(diagnostic)*diagnostic=staged.errorString();return false;}return staged.commit();
}
bool DatabaseWorkbook::importXlsx(const DatabaseDocument& document,const QString& path,SDatabaseWorkbookImport* result,QString* diagnostic)
{
    if(!result)return false;QZipReader zip(path);if(!zip.exists()){if(diagnostic)*diagnostic="The XLSX package cannot be opened.";return false;}const QStringList keys=physicalKeys(document);const QByteArray workbookManifest=zip.fileData("ei-mapper/manifest.txt");if(!workbookManifest.isEmpty()&&workbookManifest!=manifest(document,keys)){if(diagnostic)*diagnostic="This workbook does not match the active profile, source archive, or full table schema.";return false;}const QStringList strings=readSharedStrings(zip.fileData("xl/sharedStrings.xml"));const auto sheets=workbookSheets(zip.fileData("xl/workbook.xml")),relationships=workbookRelationships(zip.fileData("xl/_rels/workbook.xml.rels"));QMap<QString,SDatabaseTable> tables;for(const QString& key:document.tableKeys())tables[key]=*document.table(key);
    for(int i=0;i<keys.size();++i)
    {
        const SDatabaseTable* original=document.table(keys[i]);const auto cols=columns(*original);SDatabaseTable candidate=*original;candidate.rows.clear();const QString worksheet=worksheetForTable(*original,sheets,relationships);const auto map=cells(zip.fileData(worksheet),strings,diagnostic);if(map.isEmpty()){if(diagnostic)*diagnostic=QString("Sheet '%1' is missing or invalid.").arg(sheetName(*original));return false;}
        int headerRow=-1;for(int row=1;row<=32&&headerRow<0;++row)for(auto it=map.constBegin();it!=map.constEnd();++it)if(it.key().endsWith(QString::number(row))&&it.value().startsWith("FLD")){headerRow=row;break;}if(headerRow<0){if(diagnostic)*diagnostic=QString("Sheet '%1' has no EIDB FLD header row.").arg(sheetName(*original));return false;}
        QMap<QString,int> headerColumns;for(int c=0;c<2048;++c){const QString value=map.value(column(c)+QString::number(headerRow));if(!value.isEmpty())headerColumns[value]=c;}QVector<int> workbookColumns;for(int c=0;c<cols.size();++c){const QString expected=QString("FLD%1-%2").arg(cols[c].field).arg(cols[c].index);workbookColumns.append(headerColumns.value(expected,-1));}
        const int sourceColumn=headerColumns.value("_sourceRecord",-1),nameColumn=headerColumns.value("_sourceName",-1);QSet<int> usedSources;
        for(int outputRow=headerRow+1;;++outputRow)
        {
            bool populated=false;for(int c=0;c<cols.size();++c)if(workbookColumns[c]>=0)populated|=map.contains(column(workbookColumns[c])+QString::number(outputRow));if(!populated)break;
            bool sourceOk=false;int sourceIndex=sourceColumn>=0?map.value(column(sourceColumn)+QString::number(outputRow)).toInt(&sourceOk):-1;if(!sourceOk&&original->identityFieldId>=0){int identityColumn=-1;for(int c=0;c<cols.size();++c)if(cols[c].field==original->identityFieldId&&cols[c].index==0){identityColumn=workbookColumns[c];break;}const QString identity=identityColumn>=0?map.value(column(identityColumn)+QString::number(outputRow)):QString(),identityKey=QString("fld.%1.0").arg(original->identityFieldId);for(int index=0;index<original->rows.size();++index)if(!usedSources.contains(index)&&original->rows[index].values.value(identityKey).toString().compare(identity,Qt::CaseInsensitive)==0){sourceIndex=index;sourceOk=true;break;}}const SDatabaseRow* source=sourceOk&&sourceIndex>=0&&sourceIndex<original->rows.size()?&original->rows.at(sourceIndex):nullptr;SDatabaseRow row=source?*source:SDatabaseRow();row.added=!source;row.deleted=false;row.sourceRecordIndex=source?sourceIndex:-1;row.sourceStableKey=source?source->sourceStableKey:(nameColumn>=0?map.value(column(nameColumn)+QString::number(outputRow)):QString());
            if(source&&usedSources.contains(sourceIndex)){if(diagnostic)*diagnostic=QString("Sheet %1 contains source record %2 more than once.").arg(i+1).arg(sourceIndex);return false;}if(source)usedSources.insert(sourceIndex);
            QMap<int,QVector<QPair<int,QString>>> edits;for(int c=0;c<cols.size();++c){if(workbookColumns[c]<0)continue;const QString ref=column(workbookColumns[c])+QString::number(outputRow);if(!map.contains(ref))continue;const QString key=QString("fld.%1.0").arg(cols[c].field);const QVariant baseline=compatibilityValue(row.values.value(key),cols[c]);if(!compatibilityEqual(map.value(ref),baseline,cols[c]))edits[cols[c].field].append(qMakePair(cols[c].index,map.value(ref)));}for(auto edit=edits.constBegin();edit!=edits.constEnd();++edit){const QString key=QString("fld.%1.0").arg(edit.key());const auto* storage=DatabaseSchema::instance().field(original->schemaFamily,original->blockId,edit.key());if(storage)row.values[key]=compatibilityEditedValue(row.values.value(key),*storage,edit.value());}
            const QString identityKey=original->identityFieldId>=0?QString("fld.%1.0").arg(original->identityFieldId):QString();const QString identity=row.values.value(identityKey).toString().trimmed();row.stableKey=!identity.isEmpty()?identity.toLower()+QString("#%1").arg(source?sourceIndex:outputRow):QString("record.%1").arg(source?sourceIndex:outputRow);if(row.added){int clone=-1;for(int index=0;index<original->rows.size();++index)if(original->rows[index].sourceStableKey.compare(row.sourceStableKey,Qt::CaseInsensitive)==0){clone=index;break;}if(clone<0){if(diagnostic)*diagnostic=QString("New row '%1' has no valid clone source.").arg(identity);return false;}const QMap<QString,QVariant> edited=row.values;row=original->rows.at(clone);row.values=edited;row.sourceRecordIndex=clone;row.added=true;row.deleted=false;row.stableKey=identity.toLower()+QString("#new%1").arg(outputRow);}
            candidate.rows.append(row);
        }
        for(int sourceIndex=0;sourceIndex<original->rows.size();++sourceIndex)if(!usedSources.contains(sourceIndex)){SDatabaseRow removed=original->rows.at(sourceIndex);removed.deleted=true;candidate.rows.append(removed);}tables[keys[i]]=candidate;
    }
    result->tables=tables;return true;
}
}
