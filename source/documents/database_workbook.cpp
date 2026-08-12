#include "database_workbook.h"
#include <QBuffer>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QtGui/private/qzipreader_p.h>
#include <QtGui/private/qzipwriter_p.h>

namespace {
QString xml(const QString& value){return value.toHtmlEscaped();}
QString column(int index){QString out;for(++index;index;index=(index-1)/26)out.prepend(QChar('A'+(index-1)%26));return out;}
QString cell(const QString& ref,const QVariant& value,ei::EDatabaseFieldType type)
{
    if(type==ei::EDatabaseFieldType::Integer||type==ei::EDatabaseFieldType::Decimal)return QString("<c r=\"%1\"><v>%2</v></c>").arg(ref,xml(value.toString()));
    return QString("<c r=\"%1\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%2</t></is></c>").arg(ref,xml(value.type()==QVariant::StringList?value.toStringList().join("\n"):value.toString()));
}
QByteArray sheetXml(const ei::SDatabaseTable& table)
{
    QString x="<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"3\" topLeftCell=\"A4\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews><sheetData>";
    x+="<row r=\"1\">"+cell("A1","EI Mapper Database Workbook v1",ei::EDatabaseFieldType::Text)+cell("B1",table.key,ei::EDatabaseFieldType::Text)+"</row><row r=\"2\">";
    for(int c=0;c<table.fields.size();++c)x+=cell(column(c)+"2",table.fields[c].key,ei::EDatabaseFieldType::Text);x+=cell(column(table.fields.size())+"2","_sourceName",ei::EDatabaseFieldType::Text)+"</row><row r=\"3\">";
    for(int c=0;c<table.fields.size();++c)x+=cell(column(c)+"3",QString::number(int(table.fields[c].type)),ei::EDatabaseFieldType::Text);x+=cell(column(table.fields.size())+"3","protected clone source",ei::EDatabaseFieldType::Text)+"</row>";
    int r=4;for(const auto& row:table.rows)if(!row.deleted){x+=QString("<row r=\"%1\">").arg(r);for(int c=0;c<table.fields.size();++c)x+=cell(column(c)+QString::number(r),row.values.value(table.fields[c].key),table.fields[c].type);x+=cell(column(table.fields.size())+QString::number(r),row.sourceStableKey.isEmpty()?row.values.value("name"):row.sourceStableKey,ei::EDatabaseFieldType::Text)+"</row>";++r;}
    x+="</sheetData><autoFilter ref=\"A2:"+column(table.fields.size())+QString::number(qMax(3,r-1))+"\"/></worksheet>";return x.toUtf8();
}
QMap<QString,QString> cells(const QByteArray& bytes,QString* diagnostic)
{
    QMap<QString,QString> out;QXmlStreamReader r(bytes);QString ref,value;bool inCell=false;
    while(!r.atEnd()){r.readNext();if(r.isStartElement()&&r.name()=="c"){ref=r.attributes().value("r").toString();value.clear();inCell=true;}else if(inCell&&r.isStartElement()&&(r.name()=="v"||r.name()=="t"))value+=r.readElementText();else if(r.isEndElement()&&r.name()=="c"){out[ref]=value;inCell=false;}}
    if(r.hasError()&&diagnostic)*diagnostic=r.errorString();return out;
}
QString sheetTitle(const QString& key){return key=="raceModels"?"Race Models":(key=="monsters"?"Monsters":"Items");}
}
namespace ei {
bool DatabaseWorkbook::exportXlsx(const DatabaseDocument& document,const QString& path,QString* diagnostic)
{
    QBuffer package;package.open(QIODevice::WriteOnly);QZipWriter zip(&package);zip.setCompressionPolicy(QZipWriter::AutoCompress);
    const QStringList keys=document.tableKeys();QString types="<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>";QString workbook="<?xml version=\"1.0\" encoding=\"UTF-8\"?><workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>";QString rels="<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    for(int i=0;i<keys.size();++i){types+=QString("<Override PartName=\"/xl/worksheets/sheet%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>").arg(i+1);workbook+=QString("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>").arg(sheetTitle(keys[i])).arg(i+1);rels+=QString("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%1.xml\"/>").arg(i+1);zip.addFile(QString("xl/worksheets/sheet%1.xml").arg(i+1),sheetXml(*document.table(keys[i])));}
    types+="</Types>";workbook+="</sheets></workbook>";rels+="</Relationships>";zip.addFile("[Content_Types].xml",types.toUtf8());zip.addFile("_rels/.rels","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>");zip.addFile("xl/workbook.xml",workbook.toUtf8());zip.addFile("xl/_rels/workbook.xml.rels",rels.toUtf8());zip.close();if(zip.status()!=QZipWriter::NoError){if(diagnostic)*diagnostic="Could not create the XLSX package.";return false;}QSaveFile staged(path);if(!staged.open(QIODevice::WriteOnly)){if(diagnostic)*diagnostic=staged.errorString();return false;}if(staged.write(package.data())!=package.data().size()){if(diagnostic)*diagnostic=staged.errorString();return false;}return staged.commit();
}
bool DatabaseWorkbook::importXlsx(const DatabaseDocument& document,const QString& path,SDatabaseWorkbookImport* result,QString* diagnostic)
{
    if(!result)return false;QZipReader zip(path);if(!zip.exists()){if(diagnostic)*diagnostic="The XLSX package cannot be opened.";return false;}QMap<QString,SDatabaseTable> tables;const QStringList keys=document.tableKeys();
    for(int i=0;i<keys.size();++i){const auto* original=document.table(keys[i]);SDatabaseTable table=*original;table.rows.clear();const auto map=cells(zip.fileData(QString("xl/worksheets/sheet%1.xml").arg(i+1)),diagnostic);if(map.value("A1")!="EI Mapper Database Workbook v1"||map.value("B1")!=keys[i]){if(diagnostic)*diagnostic=QString("Sheet %1 is missing EI Mapper schema metadata.").arg(i+1);return false;}QMap<int,QString> fields;for(int c=0;c<original->fields.size();++c){const QString key=map.value(column(c)+"2");if(key!=original->fields[c].key){if(diagnostic)*diagnostic=QString("Sheet %1 column %2 changed or moved.").arg(i+1).arg(c+1);return false;}fields[c]=key;}
        if(map.value(column(original->fields.size())+"2")!="_sourceName"){if(diagnostic)*diagnostic=QString("Sheet %1 is missing the protected clone-source column.").arg(i+1);return false;}
        for(int r=4;;++r){if(!map.contains("A"+QString::number(r)))break;SDatabaseRow row;for(int c=0;c<original->fields.size();++c){const auto& schema=original->fields[c];const QString value=map.value(column(c)+QString::number(r));if(schema.type==EDatabaseFieldType::Integer)row.values[schema.key]=value.toInt();else if(schema.type==EDatabaseFieldType::Decimal)row.values[schema.key]=value.toDouble();else if(schema.type==EDatabaseFieldType::TextList)row.values[schema.key]=value.split('\n',QString::SkipEmptyParts);else row.values[schema.key]=value;}row.stableKey=row.values.value("name").toString().trimmed().toLower();row.sourceStableKey=map.value(column(original->fields.size())+QString::number(r));const SDatabaseRow* source=nullptr;for(const auto& candidate:original->rows)if(candidate.stableKey==row.stableKey){source=&candidate;break;}if(source)row.sourceStableKey=source->sourceStableKey;else{row.added=true;bool cloneSource=false;for(const auto& candidate:original->rows)cloneSource|=candidate.stableKey==row.sourceStableKey.trimmed().toLower();if(!cloneSource){if(diagnostic)*diagnostic=QString("New row '%1' has no valid clone source. Copy an existing row before changing its Name.").arg(row.values.value("name").toString());return false;}}table.rows<<row;}
        for(const auto& old:original->rows){bool found=false;for(const auto& row:table.rows)found|=row.stableKey==old.stableKey;if(!found){SDatabaseRow removed=old;removed.deleted=true;table.rows<<removed;}}tables[keys[i]]=table;}
    result->tables=tables;return true;
}
}
