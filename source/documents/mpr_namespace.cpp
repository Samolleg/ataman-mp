#include "mpr_document.h"
#include <QRegularExpression>
#include <QSet>
#include <QTextCodec>
#include <QtEndian>
#include <limits>
#include "endurance_document.h"

namespace {
constexpr quint32 kSignature=0x019CE23C; constexpr int kHeaderSize=16; constexpr int kEntrySize=22;
void put16(QByteArray* b,int o,quint16 v){qToLittleEndian<quint16>(v,reinterpret_cast<uchar*>(b->data()+o));}
void put32(QByteArray* b,int o,quint32 v){qToLittleEndian<quint32>(v,reinterpret_cast<uchar*>(b->data()+o));}
quint32 nameHash(const QByteArray& encoded,quint32 count){
    QTextCodec* codec=QTextCodec::codecForName("CP1251");
    const QByteArray lower=codec?codec->fromUnicode(codec->toUnicode(encoded).toLower()):encoded.toLower();
    quint32 sum=0; for(unsigned char byte:lower)sum+=byte; return count?sum%count:0;
}
}

bool CMprDocument::applyIsolatedNamespaceTransaction(
    const CMprEditTransaction& transaction,const QString& isolatedNamespace,
    QByteArray* output,CValidationReport* report) const
{
    if(output)output->clear();
    const QRegularExpression portable(QStringLiteral("^[a-z0-9_]+$"),
        QRegularExpression::CaseInsensitiveOption);
    if(!output||!m_mapInfo.valid||!portable.match(isolatedNamespace).hasMatch()){
        if(report)report->add("mpr.isolated_namespace_input",EValidationSeverity::Error,
            "A loaded MPR, output buffer, and portable ASCII namespace are required");
        return false;
    }
    QByteArray patched;
    if(!applyTransaction(transaction,&patched,report))return false;
    CMprDocument source; CValidationReport sourceReport;
    if(!source.loadBytes(patched,QStringLiteral("isolated-source.mpr"),&sourceReport)
       ||sourceReport.hasErrors())return false;
    QMap<int,SMprSectorInfo> sectors;
    for(const SMprSectorInfo& sector:source.sectors())sectors.insert(sector.memberIndex,sector);
    QTextCodec* codec=QTextCodec::codecForName("CP1251");
    struct Member{QByteArray name,payload;quint32 time{0},offset{0},nameOffset{0};};
    QVector<Member> members; QByteArray names;
    for(const SMprArchiveMember& original:source.members()){
        QString name=original.name;
        if(original.kind==EMprMemberKind::MapParameters)name=isolatedNamespace+QStringLiteral(".MP");
        else if(original.kind==EMprMemberKind::Sector&&sectors.contains(original.tableIndex)){
            const SMprSectorInfo sector=sectors.value(original.tableIndex);
            name=QStringLiteral("%1%2%3.SEC").arg(isolatedNamespace)
                .arg(sector.sectorX,3,10,QLatin1Char('0')).arg(sector.sectorY,3,10,QLatin1Char('0'));
        }
        Member member; member.name=codec?codec->fromUnicode(name):name.toLatin1();
        if(member.name.isEmpty()||(codec&&codec->toUnicode(member.name)!=name)
           ||member.name.size()>std::numeric_limits<quint16>::max())return false;
        member.payload=original.payload;member.time=original.lastWriteTime;members.append(member);
    }
    QByteArray candidate(kHeaderSize,'\0');
    for(Member& member:members){
        candidate.append(QByteArray((16-candidate.size()%16)%16,'\0'));
        member.offset=quint32(candidate.size());member.nameOffset=quint32(names.size());
        candidate.append(member.payload);names.append(member.name);
    }
    candidate.append(QByteArray((16-candidate.size()%16)%16,'\0'));
    const quint32 tableOffset=quint32(candidate.size()),count=quint32(members.size());
    QVector<int> tableSlots(int(count),-1);QVector<quint32> next(int(count),quint32(-1));int freeSlot=int(count)-1;
    for(int memberIndex=0;memberIndex<members.size();++memberIndex){
        int slot=int(nameHash(members.at(memberIndex).name,count));
        if(tableSlots.at(slot)>=0){while(next.at(slot)!=quint32(-1))slot=int(next.at(slot));
            while(freeSlot>=0&&tableSlots.at(freeSlot)>=0)--freeSlot;if(freeSlot<0)return false;
            next[slot]=quint32(freeSlot);slot=freeSlot--;}
        tableSlots[slot]=memberIndex;
    }
    QByteArray table(int(count)*kEntrySize,'\0');
    for(int slot=0;slot<tableSlots.size();++slot){
        if(tableSlots.at(slot)<0)return false;const Member& member=members.at(tableSlots.at(slot));const int entry=slot*kEntrySize;
        put32(&table,entry,next.at(slot));put32(&table,entry+4,quint32(member.payload.size()));
        put32(&table,entry+8,member.offset);put32(&table,entry+12,member.time);
        put16(&table,entry+16,quint16(member.name.size()));put32(&table,entry+18,member.nameOffset);
    }
    for(int memberIndex=0;memberIndex<members.size();++memberIndex){
        int slot=int(nameHash(members.at(memberIndex).name,count));QSet<int> visited;bool found=false;
        while(slot>=0&&slot<tableSlots.size()&&!visited.contains(slot)){
            visited.insert(slot);if(tableSlots.at(slot)==memberIndex){found=true;break;}
            slot=next.at(slot)==quint32(-1)?-1:int(next.at(slot));
        }
        if(!found){if(report)report->add("mpr.isolated_namespace_hash",EValidationSeverity::Error,
            "A renamed member is unreachable through the rebuilt runtime hash chain");return false;}
    }
    candidate.append(table);candidate.append(names);put32(&candidate,0,kSignature);put32(&candidate,4,count);
    put32(&candidate,8,tableOffset);put32(&candidate,12,quint32(names.size()));
    CMprDocument reopened;CValidationReport reopenReport;
    if(!reopened.loadBytes(candidate,QStringLiteral("isolated-candidate.mpr"),&reopenReport)
       ||reopenReport.hasErrors()||reopened.mapInfo().innerNamespace.compare(isolatedNamespace,Qt::CaseInsensitive)!=0){
        if(report)report->add("mpr.isolated_namespace_reopen",EValidationSeverity::Error,
            "The isolated MPR failed fresh semantic reopen");return false;
    }
    *output=candidate;return true;
}
