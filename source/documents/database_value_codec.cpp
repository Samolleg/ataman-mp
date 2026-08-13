#include "database_value_codec.h"

#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCodec>
#include <cstring>
#include <cmath>

namespace {
quint32 read32(const QByteArray& b,int o){return quint32(uchar(b[o]))|(quint32(uchar(b[o+1]))<<8)|(quint32(uchar(b[o+2]))<<16)|(quint32(uchar(b[o+3]))<<24);}
void append32(QByteArray* b,quint32 v){b->append(char(v));b->append(char(v>>8));b->append(char(v>>16));b->append(char(v>>24));}
QByteArray header(int id,int size){QByteArray b;b.append(char(id));const quint32 n=quint32(size)*2;if(n<=254)b.append(char(n));else append32(&b,n+1);return b;}
struct Element{int id{0};QByteArray encoded,payload;};
QVector<Element> elements(const QByteArray& payload,bool* ok=nullptr)
{
    QVector<Element> out;int offset=0;while(offset<payload.size()){if(offset+2>payload.size()){if(ok)*ok=false;return {};}const int start=offset,id=uchar(payload[offset++]);quint32 n=uchar(payload[offset++]);if(n&1){if(offset+3>payload.size()){if(ok)*ok=false;return {};}n|=quint32(uchar(payload[offset]))<<8;n|=quint32(uchar(payload[offset+1]))<<16;n|=quint32(uchar(payload[offset+2]))<<24;offset+=3;n-=1;}const int size=int(n/2);if(size<0||offset+size>payload.size()){if(ok)*ok=false;return {};}Element e;e.id=id;e.payload=payload.mid(offset,size);offset+=size;e.encoded=payload.mid(start,offset-start);out.append(e);}if(ok)*ok=true;return out;
}
QString text(const QByteArray& payload){QByteArray b=payload;while(!b.isEmpty()&&b.endsWith('\0'))b.chop(1);return QTextCodec::codecForName("CP1251")->toUnicode(b);}
QByteArray encodedText(const QString& value,bool fixed,int size)
{
    QByteArray out=QTextCodec::codecForName("CP1251")->fromUnicode(value);if(fixed){if(out.size()>=size)out.truncate(qMax(0,size-1));out.append(QByteArray(qMax(0,size-out.size()),'\0'));}else if(out.isEmpty()||!out.endsWith('\0'))out.append('\0');return out;
}
QJsonValue scalar(const QByteArray& p,ei::EDatabaseStorageType type)
{
    if(type==ei::EDatabaseStorageType::String||type==ei::EDatabaseStorageType::FixedString)return text(p);
    if(type==ei::EDatabaseStorageType::Byte)return p.isEmpty()?0:int(uchar(p[0]));
    if((type==ei::EDatabaseStorageType::SignedLong||type==ei::EDatabaseStorageType::UnsignedLong)&&p.size()>=4)return type==ei::EDatabaseStorageType::SignedLong?QJsonValue(qint32(read32(p,0))):QJsonValue(double(read32(p,0)));
    if(type==ei::EDatabaseStorageType::Float&&p.size()>=4){float value;memcpy(&value,p.constData(),4);if(!std::isfinite(value))return QString("#float:%1").arg(QString::fromLatin1(p.toHex()));return double(value);}
    return QString::fromLatin1(p.toHex());
}
QByteArray scalarBytes(const QJsonValue& value,ei::EDatabaseStorageType type,int modifier,QString* diagnostic)
{
    QByteArray out;if(type==ei::EDatabaseStorageType::String)return encodedText(value.toString(),false,0);if(type==ei::EDatabaseStorageType::FixedString)return encodedText(value.toString(),true,modifier);
    if(type==ei::EDatabaseStorageType::Byte){out.append(char(value.toInt()));return out;}
    if(type==ei::EDatabaseStorageType::SignedLong||type==ei::EDatabaseStorageType::UnsignedLong){append32(&out,quint32(type==ei::EDatabaseStorageType::SignedLong?qint32(value.toInt()):quint32(value.toDouble())));return out;}
    if(type==ei::EDatabaseStorageType::Float){if(value.isString()&&value.toString().startsWith("#float:"))return QByteArray::fromHex(value.toString().mid(7).toLatin1());float f=float(value.toDouble());out.append(reinterpret_cast<const char*>(&f),4);return out;}
    out=QByteArray::fromHex(value.toString().toLatin1());if(out.isEmpty()&&!value.toString().isEmpty()&&diagnostic)*diagnostic="Invalid hexadecimal value.";return out;
}
QJsonDocument json(const QVariant& value,QString* diagnostic)
{
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(value.toString().toUtf8(),&error);if(error.error!=QJsonParseError::NoError&&diagnostic)*diagnostic=error.errorString();return doc;
}
}

namespace ei {
bool DatabaseValueCodec::editable(const SDatabaseStorageField&){return true;}
QVariant DatabaseValueCodec::decode(const QByteArray& p,const SDatabaseStorageField& f,const DatabaseSchema& schema,const QString& family,QString* diagnostic)
{
    if(f.type==EDatabaseStorageType::String||f.type==EDatabaseStorageType::FixedString)return text(p);
    if(f.type==EDatabaseStorageType::SignedLong)return p.size()>=4?QVariant(qint32(read32(p,0))):QVariant();
    if(f.type==EDatabaseStorageType::UnsignedLong)return p.size()>=4?QVariant(quint32(read32(p,0))):QVariant();
    if(f.type==EDatabaseStorageType::Byte)return p.isEmpty()?QVariant():QVariant(uchar(p[0]));
    if(f.type==EDatabaseStorageType::Float){if(p.size()<4)return {};float value;memcpy(&value,p.constData(),4);return QVariant::fromValue(value);}
    if(f.type==EDatabaseStorageType::Hex)return QString::fromLatin1(p.toHex());
    QJsonArray output;
    if(f.type==EDatabaseStorageType::FloatList||f.type==EDatabaseStorageType::UnsignedLongList||f.type==EDatabaseStorageType::ByteList)
    {
        const int width=f.type==EDatabaseStorageType::ByteList?1:4;for(int i=0;i+width<=p.size();i+=width)output.append(scalar(p.mid(i,width),f.type==EDatabaseStorageType::FloatList?EDatabaseStorageType::Float:(f.type==EDatabaseStorageType::UnsignedLongList?EDatabaseStorageType::UnsignedLong:EDatabaseStorageType::Byte)));
    }
    else if(f.type==EDatabaseStorageType::BitList){for(int i=0;i<f.modifier&&i<p.size()*8;++i)output.append(bool(uchar(p[i/8])&(1u<<(i%8))));}
    else if(f.type==EDatabaseStorageType::TypeList)
    {
        bool ok=false;const auto nested=elements(p,&ok);if(!ok){if(diagnostic)*diagnostic="Malformed typed-list database field.";return {};}
        for(const auto& child:nested){const auto* nestedField=schema.field(family,f.modifier,child.id);QJsonObject field;field["id"]=child.id;field["value"]=nestedField?scalar(child.payload,nestedField->type):QJsonValue(QString::fromLatin1(child.payload.toHex()));output.append(field);}
    }
    else
    {
        bool ok=false;const auto nested=elements(p,&ok);if(!ok){if(diagnostic)*diagnostic="Malformed nested database field.";return {};}
        for(const auto& e:nested)
        {
            if(f.type==EDatabaseStorageType::IndexedFloatList){QJsonObject o;o["id"]=e.id;o["value"]=scalar(e.payload,EDatabaseStorageType::Float);output.append(o);continue;}
            if(f.type==EDatabaseStorageType::StringList||f.type==EDatabaseStorageType::FixedStringList){output.append(text(e.payload));continue;}
            const int block=f.modifier;QJsonObject object;object["id"]=e.id;QJsonArray fields;bool childrenOk=false;const auto children=elements(e.payload,&childrenOk);if(!childrenOk){object["raw"]=QString::fromLatin1(e.payload.toHex());output.append(object);continue;}for(const auto& child:children){const auto* nestedField=schema.field(family,block,child.id);QJsonObject field;field["id"]=child.id;field["value"]=nestedField?scalar(child.payload,nestedField->type):QJsonValue(QString::fromLatin1(child.payload.toHex()));fields.append(field);}object["fields"]=fields;output.append(object);
        }
    }
    return QString::fromUtf8(QJsonDocument(output).toJson(QJsonDocument::Compact));
}
QByteArray DatabaseValueCodec::encode(const QVariant& value,const QByteArray& source,const SDatabaseStorageField& f,const DatabaseSchema& schema,const QString& family,QString* diagnostic)
{
    if(f.type==EDatabaseStorageType::String)return encodedText(value.toString(),false,0);if(f.type==EDatabaseStorageType::FixedString)return encodedText(value.toString(),true,f.modifier);
    if(f.type==EDatabaseStorageType::Hex)return QByteArray::fromHex(value.toString().toLatin1());
    if(f.type==EDatabaseStorageType::SignedLong||f.type==EDatabaseStorageType::UnsignedLong||f.type==EDatabaseStorageType::Byte||f.type==EDatabaseStorageType::Float)return scalarBytes(QJsonValue::fromVariant(value),f.type,f.modifier,diagnostic);
    const QJsonDocument doc=json(value,diagnostic);if(!doc.isArray())return {};const QJsonArray values=doc.array();QByteArray out;
    if(f.type==EDatabaseStorageType::FloatList||f.type==EDatabaseStorageType::UnsignedLongList||f.type==EDatabaseStorageType::ByteList){const auto scalarType=f.type==EDatabaseStorageType::FloatList?EDatabaseStorageType::Float:(f.type==EDatabaseStorageType::UnsignedLongList?EDatabaseStorageType::UnsignedLong:EDatabaseStorageType::Byte);for(const auto& v:values)out+=scalarBytes(v,scalarType,0,diagnostic);return out;}
    if(f.type==EDatabaseStorageType::BitList){out=QByteArray(qMax(source.size(),(f.modifier+7)/8),'\0');for(int i=0;i<values.size()&&i<f.modifier;++i)if(values[i].toBool())out[i/8]=char(uchar(out[i/8])|(1u<<(i%8)));return out;}
    if(f.type==EDatabaseStorageType::TypeList)
    {
        for(const auto& item:values){const QJsonObject object=item.toObject();const int id=object.value("id").toInt();const auto* nestedField=schema.field(family,f.modifier,id);const QByteArray payload=nestedField?scalarBytes(object.value("value"),nestedField->type,nestedField->modifier,diagnostic):QByteArray::fromHex(object.value("value").toString().toLatin1());out+=header(id,payload.size())+payload;}return out;
    }
    const auto originals=elements(source);for(int i=0;i<values.size();++i)
    {
        int id=i<originals.size()?originals[i].id:(originals.isEmpty()?1:originals.last().id);QByteArray payload;
        if(f.type==EDatabaseStorageType::StringList)payload=encodedText(values[i].toString(),false,0);
        else if(f.type==EDatabaseStorageType::FixedStringList)payload=encodedText(values[i].toString(),true,qMax(f.modifier,1));
        else if(f.type==EDatabaseStorageType::IndexedFloatList){const QJsonObject o=values[i].toObject();id=o.value("id").toInt(id);payload=scalarBytes(o.value("value"),EDatabaseStorageType::Float,0,diagnostic);}
        else {const QJsonObject o=values[i].toObject();id=o.value("id").toInt(id);if(o.contains("raw"))payload=QByteArray::fromHex(o.value("raw").toString().toLatin1());else for(const auto& fv:o.value("fields").toArray()){const QJsonObject fo=fv.toObject();const int childId=fo.value("id").toInt();const auto* nf=schema.field(family,f.modifier,childId);const QByteArray child=nf?scalarBytes(fo.value("value"),nf->type,nf->modifier,diagnostic):QByteArray::fromHex(fo.value("value").toString().toLatin1());payload+=header(childId,child.size())+child;}}
        out+=header(id,payload.size())+payload;
    }
    return out;
}
}
