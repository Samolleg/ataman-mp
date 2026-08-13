#include "lossless_database_member.h"
#include <QMap>

namespace {
quint32 u32(const QByteArray& b,int o){return quint32(uchar(b[o]))|(quint32(uchar(b[o+1]))<<8)|(quint32(uchar(b[o+2]))<<16)|(quint32(uchar(b[o+3]))<<24);}
void put32(QByteArray* b,quint32 v){b->append(char(v));b->append(char(v>>8));b->append(char(v>>16));b->append(char(v>>24));}
}

namespace ei {
LosslessDatabaseMember LosslessDatabaseMember::parse(const QByteArray& bytes,QString* diagnostic)
{
    LosslessDatabaseMember result; result.m_original=bytes; int next=0;
    result.m_valid=parseNode(bytes,0,bytes.size(),&result.m_root,&next,0,true,diagnostic);
    if(result.m_valid)result.m_suffix=bytes.mid(next);
    if(!result.m_valid&&diagnostic&&diagnostic->isEmpty())*diagnostic="Database member has trailing or invalid data.";
    return result;
}

bool LosslessDatabaseMember::parseNode(const QByteArray& bytes,int offset,int limit,Node* node,int* next,int depth,bool requireChildren,QString* diagnostic)
{
    if(depth>16||offset<0||offset+2>limit){if(diagnostic)*diagnostic="Invalid tag boundary.";return false;}
    const int id=uchar(bytes[offset]);quint32 encoded=uchar(bytes[offset+1]);int headerSize=2;
    if(encoded&1){if(offset+5>limit)return false;encoded=u32(bytes,offset+1)-1;headerSize=5;}
    const quint32 length=encoded/2;if(length>quint32(limit-offset-headerSize))return false;
    node->id=id;node->header=bytes.mid(offset,headerSize);node->payload=bytes.mid(offset+headerSize,int(length));
    const int end=offset+headerSize+int(length);*next=end;
    if(!requireChildren)return true;
    int cursor=offset+headerSize;QVector<Node> children;
    while(cursor<end){Node child;int childNext=cursor;if(!parseNode(bytes,cursor,end,&child,&childNext,depth+1,depth<2,diagnostic))return false;children.append(child);cursor=childNext;}
    if(cursor!=end)return false;
    node->children=children;node->container=true;return true;
}

QByteArray LosslessDatabaseMember::encodeHeader(int id,int size)
{
    QByteArray out;out.append(char(id));quint32 encoded=quint32(size)*2;
    if(encoded<=254)out.append(char(encoded));else put32(&out,encoded+1);return out;
}
QByteArray LosslessDatabaseMember::encodeNode(const Node& node)
{
    if(!node.dirty)return node.header+node.payload;
    QByteArray payload;if(node.container)for(const Node& child:node.children)payload+=encodeNode(child);else payload=node.payload;
    // Retain the exact original tag header whenever its encoded payload length
    // remains valid. This preserves non-canonical-but-valid short/long headers
    // and limits fixed-size edits to the field payload bytes themselves.
    bool headerMatches=false;
    if(node.header.size()==2)
        headerMatches=uchar(node.header.at(0))==node.id
            && !(uchar(node.header.at(1))&1)
            && uchar(node.header.at(1))/2==payload.size();
    else if(node.header.size()==5)
        headerMatches=uchar(node.header.at(0))==node.id
            && (u32(node.header,1)-1)/2==quint32(payload.size());
    return (headerMatches?node.header:encodeHeader(node.id,payload.size()))+payload;
}
bool LosslessDatabaseMember::valid()const{return m_valid;}
QByteArray LosslessDatabaseMember::serialize()const{return m_valid?encodeNode(m_root)+m_suffix:QByteArray();}
int LosslessDatabaseMember::tableCount()const{return m_valid?m_root.children.size():0;}
int LosslessDatabaseMember::recordCount(int tableIndex)const
{
    return m_valid&&tableIndex>=0&&tableIndex<m_root.children.size()
        ?m_root.children.at(tableIndex).children.size():0;
}
const LosslessDatabaseMember::Node* LosslessDatabaseMember::recordNode(int tableIndex,int recordIndex)const
{
    if(!m_valid||tableIndex<0||tableIndex>=m_root.children.size())return nullptr;
    const Node& table=m_root.children[tableIndex];
    return recordIndex>=0&&recordIndex<table.children.size()?&table.children[recordIndex]:nullptr;
}
QVector<SLosslessDatabaseField> LosslessDatabaseMember::fields(int tableIndex,int recordIndex)const
{
    QVector<SLosslessDatabaseField> out;const Node* record=recordNode(tableIndex,recordIndex);if(!record)return out;QMap<int,int> occurrences;
    for(const Node& field:record->children){int occurrence=occurrences[field.id]++;out.append({field.id,occurrence,field.payload,field.header+field.payload});}return out;
}
int LosslessDatabaseMember::findRecordByFieldPayload(int tableIndex,int fieldId,const QByteArray& payload)const
{
    if(!m_valid||tableIndex<0||tableIndex>=m_root.children.size())return -1;
    const Node& table=m_root.children[tableIndex];int result=-1;
    for(int recordIndex=0;recordIndex<table.children.size();++recordIndex)
    {
        const Node& record=table.children[recordIndex];bool matches=false;
        for(const Node& field:record.children)
            if(field.id==fieldId&&field.payload==payload){matches=true;break;}
        if(!matches)continue;
        if(result>=0)return -2;
        result=recordIndex;
    }
    return result;
}
int LosslessDatabaseMember::findRecordByEncodedFields(int tableIndex,const QVector<SLosslessDatabaseField>& wanted)const
{
    if(!m_valid||tableIndex<0||tableIndex>=m_root.children.size())return -1;int result=-1;
    for(int record=0;record<recordCount(tableIndex);++record)
    {
        const auto candidate=fields(tableIndex,record);if(candidate.size()!=wanted.size())continue;bool same=true;
        for(int i=0;i<candidate.size();++i)same=same&&candidate[i].encoded==wanted[i].encoded;
        if(!same)continue;if(result>=0)return -2;result=record;
    }
    return result;
}
bool LosslessDatabaseMember::cloneRecord(int tableIndex,int recordIndex,QString* diagnostic)
{
    if(!m_valid||tableIndex<0||tableIndex>=m_root.children.size()||recordIndex<0||recordIndex>=m_root.children[tableIndex].children.size()){if(diagnostic)*diagnostic="Clone source record is unavailable.";return false;}
    Node& table=m_root.children[tableIndex];table.children.append(table.children.at(recordIndex));table.dirty=true;m_root.dirty=true;return true;
}
bool LosslessDatabaseMember::removeRecord(int tableIndex,int recordIndex,QString* diagnostic)
{
    if(!m_valid||tableIndex<0||tableIndex>=m_root.children.size()||recordIndex<0||recordIndex>=m_root.children[tableIndex].children.size()){if(diagnostic)*diagnostic="Deleted record is unavailable.";return false;}
    Node& table=m_root.children[tableIndex];table.children.removeAt(recordIndex);table.dirty=true;m_root.dirty=true;return true;
}
LosslessDatabaseMember::Node* LosslessDatabaseMember::fieldNode(int tableIndex,int recordIndex,int fieldId,int occurrence)
{
    if(!m_valid||tableIndex<0||tableIndex>=m_root.children.size())return nullptr;
    Node& table=m_root.children[tableIndex];if(recordIndex<0||recordIndex>=table.children.size())return nullptr;Node& record=table.children[recordIndex];int found=0;
    for(Node& field:record.children)if(field.id==fieldId&&found++==occurrence){field.dirty=true;record.dirty=true;table.dirty=true;m_root.dirty=true;return &field;}
    return nullptr;
}
bool LosslessDatabaseMember::replaceFieldPayload(int tableIndex,int recordIndex,int fieldId,int occurrence,const QByteArray& payload,QString* diagnostic)
{
    Node* field=fieldNode(tableIndex,recordIndex,fieldId,occurrence);if(!field){if(diagnostic)*diagnostic="Requested field occurrence does not exist.";return false;}field->payload=payload;return true;
}
bool LosslessDatabaseMember::cloneRecordByFieldPayload(int tableIndex,int fieldId,const QByteArray& sourcePayload,const QByteArray& newPayload,QString* diagnostic)
{
    const int source=findRecordByFieldPayload(tableIndex,fieldId,sourcePayload);if(source<0){if(diagnostic)*diagnostic="Clone source record is missing or ambiguous.";return false;}
    Node& table=m_root.children[tableIndex];Node clone=table.children[source];table.children.append(clone);table.dirty=true;m_root.dirty=true;
    return replaceFieldPayload(tableIndex,table.children.size()-1,fieldId,0,newPayload,diagnostic);
}
bool LosslessDatabaseMember::removeRecordByFieldPayload(int tableIndex,int fieldId,const QByteArray& payload,QString* diagnostic)
{
    const int record=findRecordByFieldPayload(tableIndex,fieldId,payload);if(record<0){if(diagnostic)*diagnostic="Deleted record is missing or ambiguous.";return false;}
    Node& table=m_root.children[tableIndex];table.children.removeAt(record);table.dirty=true;m_root.dirty=true;return true;
}
}
