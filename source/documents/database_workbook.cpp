#include "database_workbook.h"
#include "database_schema.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QXmlStreamReader>
#include <QtGui/private/qzipreader_p.h>
#include <QtGui/private/qzipwriter_p.h>

namespace {
QString xml(const QString& value){return value.toHtmlEscaped();}
QString column(int index){QString out;for(++index;index;index=(index-1)/26)out.prepend(QChar('A'+(index-1)%26));return out;}
QString cell(const QString& ref,const QVariant& value,ei::EDatabaseFieldType type)
{
    if((type==ei::EDatabaseFieldType::Integer||type==ei::EDatabaseFieldType::Decimal)&&value.isValid())
        return QString("<c r=\"%1\"><v>%2</v></c>").arg(ref,xml(value.toString()));
    return QString("<c r=\"%1\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%2</t></is></c>").arg(ref,xml(value.type()==QVariant::StringList?value.toStringList().join("\n"):value.toString()));
}
QString sheetName(const ei::SDatabaseTable& table){return table.label.left(31);}
QByteArray sheetXml(const ei::SDatabaseTable& table)
{
    QString x="<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"2\" topLeftCell=\"A3\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews><sheetData><row r=\"1\">";
    for(int c=0;c<table.fields.size();++c)x+=cell(column(c)+"1",table.fields[c].label,ei::EDatabaseFieldType::Text);
    x+=cell(column(table.fields.size())+"1","_sourceRecord",ei::EDatabaseFieldType::Text)+cell(column(table.fields.size()+1)+"1","_sourceName",ei::EDatabaseFieldType::Text)+"</row><row r=\"2\">";
    for(int c=0;c<table.fields.size();++c)x+=cell(column(c)+"2",QString("FLD%1-%2").arg(table.fields[c].storageId).arg(table.fields[c].storageOccurrence),ei::EDatabaseFieldType::Text);
    x+=cell(column(table.fields.size())+"2","_sourceRecord",ei::EDatabaseFieldType::Text)+cell(column(table.fields.size()+1)+"2","_sourceName",ei::EDatabaseFieldType::Text)+"</row>";
    int outputRow=3;for(const auto& row:table.rows)if(!row.deleted){x+=QString("<row r=\"%1\">").arg(outputRow);for(int c=0;c<table.fields.size();++c)if(row.values.contains(table.fields[c].key))x+=cell(column(c)+QString::number(outputRow),row.values.value(table.fields[c].key),table.fields[c].type);x+=cell(column(table.fields.size())+QString::number(outputRow),row.sourceRecordIndex,ei::EDatabaseFieldType::Integer);x+=cell(column(table.fields.size()+1)+QString::number(outputRow),row.sourceStableKey,ei::EDatabaseFieldType::Text)+"</row>";++outputRow;}
    x+="</sheetData><autoFilter ref=\"A2:"+column(table.fields.size()+1)+QString::number(qMax(2,outputRow-1))+"\"/></worksheet>";return x.toUtf8();
}
QMap<QString,QString> cells(const QByteArray& bytes,QString* diagnostic)
{
    QMap<QString,QString> out;QXmlStreamReader r(bytes);QString ref,value;bool inCell=false;
    while(!r.atEnd()){r.readNext();if(r.isStartElement()&&r.name()=="c"){ref=r.attributes().value("r").toString();value.clear();inCell=true;}else if(inCell&&r.isStartElement()&&(r.name()=="v"||r.name()=="t"))value+=r.readElementText();else if(r.isEndElement()&&r.name()=="c"){out[ref]=value;inCell=false;}}
    if(r.hasError()&&diagnostic)*diagnostic=r.errorString();return out;
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
    QSet<QString> names;
    for(int i=0;i<keys.size();++i){const auto* table=document.table(keys[i]);QString name=sheetName(*table);if(names.contains(name)){if(diagnostic)*diagnostic="Duplicate XLSX sheet title: "+name;return false;}names.insert(name);types+=QString("<Override PartName=\"/xl/worksheets/sheet%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>").arg(i+1);workbook+=QString("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>").arg(xml(name)).arg(i+1);rels+=QString("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%1.xml\"/>").arg(i+1);zip.addFile(QString("xl/worksheets/sheet%1.xml").arg(i+1),sheetXml(*table));}
    types+="</Types>";workbook+="</sheets></workbook>";rels+="</Relationships>";zip.addFile("[Content_Types].xml",types.toUtf8());zip.addFile("_rels/.rels","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>");zip.addFile("xl/workbook.xml",workbook.toUtf8());zip.addFile("xl/_rels/workbook.xml.rels",rels.toUtf8());zip.addFile("ei-mapper/manifest.txt",manifest(document,keys));QFile source(document.sourceArchivePath());if(source.open(QIODevice::ReadOnly))zip.addFile("ei-mapper/source.res",source.readAll());zip.close();
    if(zip.status()!=QZipWriter::NoError){if(diagnostic)*diagnostic="Could not create the XLSX package.";return false;}QSaveFile staged(path);if(!staged.open(QIODevice::WriteOnly)){if(diagnostic)*diagnostic=staged.errorString();return false;}if(staged.write(package.data())!=package.data().size()){if(diagnostic)*diagnostic=staged.errorString();return false;}return staged.commit();
}
bool DatabaseWorkbook::importXlsx(const DatabaseDocument& document,const QString& path,SDatabaseWorkbookImport* result,QString* diagnostic)
{
    if(!result)return false;QZipReader zip(path);if(!zip.exists()){if(diagnostic)*diagnostic="The XLSX package cannot be opened.";return false;}const QStringList keys=physicalKeys(document);if(zip.fileData("ei-mapper/manifest.txt")!=manifest(document,keys)){if(diagnostic)*diagnostic="This workbook does not match the active profile, source archive, or full table schema.";return false;}QMap<QString,SDatabaseTable> tables;for(const QString& key:document.tableKeys())tables[key]=*document.table(key);
    for(int i=0;i<keys.size();++i)
    {
        const SDatabaseTable* original=document.table(keys[i]);SDatabaseTable candidate=*original;candidate.rows.clear();const auto map=cells(zip.fileData(QString("xl/worksheets/sheet%1.xml").arg(i+1)),diagnostic);if(map.isEmpty()){if(diagnostic)*diagnostic=QString("Sheet %1 is missing or invalid.").arg(i+1);return false;}
        for(int c=0;c<original->fields.size();++c){const QString expected=QString("FLD%1-%2").arg(original->fields[c].storageId).arg(original->fields[c].storageOccurrence);if(map.value(column(c)+"2")!=expected){if(diagnostic)*diagnostic=QString("Sheet %1 column %2 changed or moved; expected %3.").arg(i+1).arg(c+1).arg(expected);return false;}}
        const int sourceColumn=original->fields.size(),nameColumn=sourceColumn+1;QSet<int> usedSources;
        for(int outputRow=3;;++outputRow)
        {
            bool populated=false;for(int c=0;c<original->fields.size();++c)populated|=map.contains(column(c)+QString::number(outputRow));if(!populated)break;
            const int sourceIndex=map.value(column(sourceColumn)+QString::number(outputRow)).toInt();const SDatabaseRow* source=sourceIndex>=0&&sourceIndex<original->rows.size()?&original->rows.at(sourceIndex):nullptr;SDatabaseRow row=source?*source:SDatabaseRow();row.added=!source;row.deleted=false;row.sourceRecordIndex=source?sourceIndex:-1;row.sourceStableKey=map.value(column(nameColumn)+QString::number(outputRow));
            if(source&&usedSources.contains(sourceIndex)){if(diagnostic)*diagnostic=QString("Sheet %1 contains source record %2 more than once.").arg(i+1).arg(sourceIndex);return false;}if(source)usedSources.insert(sourceIndex);
            for(int c=0;c<original->fields.size();++c){const auto& field=original->fields[c];const QString ref=column(c)+QString::number(outputRow);if(!map.contains(ref)){row.values.remove(field.key);continue;}const QString value=map.value(ref);if(field.type==EDatabaseFieldType::Integer)row.values[field.key]=value.toLongLong();else if(field.type==EDatabaseFieldType::Decimal)row.values[field.key]=value.toDouble();else row.values[field.key]=value;}
            const QString identityKey=original->identityFieldId>=0?QString("fld.%1.0").arg(original->identityFieldId):QString();const QString identity=row.values.value(identityKey).toString().trimmed();row.stableKey=!identity.isEmpty()?identity.toLower()+QString("#%1").arg(source?sourceIndex:outputRow):QString("record.%1").arg(source?sourceIndex:outputRow);if(row.added){int clone=-1;for(int index=0;index<original->rows.size();++index)if(original->rows[index].sourceStableKey.compare(row.sourceStableKey,Qt::CaseInsensitive)==0){clone=index;break;}if(clone<0){if(diagnostic)*diagnostic=QString("New row '%1' has no valid clone source.").arg(identity);return false;}const QMap<QString,QVariant> edited=row.values;row=original->rows.at(clone);row.values=edited;row.sourceRecordIndex=clone;row.added=true;row.deleted=false;row.stableKey=identity.toLower()+QString("#new%1").arg(outputRow);}
            candidate.rows.append(row);
        }
        for(int sourceIndex=0;sourceIndex<original->rows.size();++sourceIndex)if(!usedSources.contains(sourceIndex)){SDatabaseRow removed=original->rows.at(sourceIndex);removed.deleted=true;candidate.rows.append(removed);}tables[keys[i]]=candidate;
    }
    result->tables=tables;return true;
}
}
