#pragma once
#include <QDataStream>
#include <QtEndian>
#include "res_archive_document.h"

namespace ei_test {
// Owned, deterministic MP/SEC data. No installed game or private fixture.
inline QByteArray terrainMpr(int columns=1,int rows=1,quint16 height=1000)
{
    QByteArray empty;QDataStream seed(&empty,QIODevice::WriteOnly);seed.setByteOrder(QDataStream::LittleEndian);
    seed<<quint32(0x019ce23c)<<quint32(0)<<quint32(16)<<quint32(0);
    CResArchiveDocument archive;if(!archive.loadBytes(empty))return {};
    QByteArray mp;QDataStream info(&mp,QIODevice::WriteOnly);
    info.setByteOrder(QDataStream::LittleEndian);info.setFloatingPointPrecision(QDataStream::SinglePrecision);
    info<<quint32(0xce4af672)<<float(100)<<quint32(columns)<<quint32(rows)
        <<quint32(2)<<quint32(512)<<quint32(3)<<quint32(64)<<quint16(2)<<quint32(0);
    for(int i=0;i<2;++i)info<<qint32(3)<<float(.2)<<float(.3)<<float(.6)<<float(.7)
        <<float(0)<<float(.5)<<float(0)<<float(0)<<float(0)<<float(0);
    info<<qint32(1)<<qint32(2)<<qint32(2);
    QByteArray sector(5+33*33*8+16*16*2,char(0));
    qToLittleEndian<quint32>(0xcf4bf774,reinterpret_cast<uchar*>(sector.data()));
    for(int i=0;i<33*33;++i)qToLittleEndian<quint16>(height,reinterpret_cast<uchar*>(sector.data()+5+i*8+2));
    QVector<SResArchiveNewMember> members;
    SResArchiveNewMember map;map.name="audit.mp";map.payload=mp;members.append(map);
    for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){
        SResArchiveNewMember member;
        member.name=QString("audit%1%2.sec").arg(x,3,10,QLatin1Char('0')).arg(y,3,10,QLatin1Char('0'));
        member.payload=sector;members.append(member);
    }
    QByteArray result;if(!archive.buildWithPublishedMembers(members,false,&result))return {};
    return result;
}
}
