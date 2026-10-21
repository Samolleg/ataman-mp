#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include "neighbor_scene_data.h"
#include "endurance_document.h"
#include "mpr_document.h"
#include "native_sha256.h"
#include <QHash>
#include <QJsonArray>
#include <QTextCodec>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <QCryptographicHash>
#include <QFile>
#include <QMutex>
#include <QDateTime>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

QByteArray neighborFileRevision(const QString& path)
{
    if(path.isEmpty()) return {};
    const QFileInfo info(path);
    QByteArray revision=(info.absoluteFilePath().toCaseFolded()+QString::number(info.size())
        +info.lastModified().toString(Qt::ISODateWithMs)).toUtf8();
    bool memoizable=false;
#ifdef Q_OS_WIN
    HANDLE handle=CreateFileW(reinterpret_cast<LPCWSTR>(info.absoluteFilePath().utf16()),
        FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle!=INVALID_HANDLE_VALUE) {
        FILE_BASIC_INFO basic{}; BY_HANDLE_FILE_INFORMATION identity{};
        if(GetFileInformationByHandleEx(handle,FileBasicInfo,&basic,sizeof(basic))
           && GetFileInformationByHandle(handle,&identity)) {
            revision+=QByteArray::number(basic.ChangeTime.QuadPart)+":"
                +QByteArray::number(basic.CreationTime.QuadPart)+":"
                +QByteArray::number(quint64(identity.dwVolumeSerialNumber))+":"
                +QByteArray::number(quint64(identity.nFileIndexHigh))+":"+QByteArray::number(quint64(identity.nFileIndexLow));
            memoizable=true;
        }
        CloseHandle(handle);
    }
#endif
    return memoizable ? revision : QByteArray();
}

QByteArray neighborFileSha256(const QString& path)
{
    if(path.isEmpty())return {};
    const QFileInfo info(path);
    const auto revision=neighborFileRevision(path);
    const bool memoizable=!revision.isEmpty();
    struct Entry { QByteArray revision,hash; };
    static QMutex mutex; QMutexLocker lock(&mutex);
    static QMap<QString,Entry> cache;
    const auto key=info.absoluteFilePath().toCaseFolded();
    if(memoizable && cache.contains(key) && cache[key].revision==revision) return cache[key].hash;
    const QString cachePath=QDir(QCoreApplication::applicationDirPath()).filePath(
        "cache/resource-sha256-v1/"+QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(),QCryptographicHash::Sha256).toHex())+".ini");
    QSettings disk(cachePath,QSettings::IniFormat);
    if(memoizable && disk.value("revision").toByteArray()==revision) {
        const auto digest=disk.value("sha256").toByteArray();
        if(digest.size()==32){cache.insert(key,{revision,digest});return digest;}
    }
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return {};
    CNativeSha256 native;
    QByteArray result;
    if(native.isAvailable()) {
        while(!file.atEnd()) {
            const auto bytes=file.read(1024*1024);
            if(bytes.isEmpty() && file.error()!=QFileDevice::NoError)return {};
            native.addData(bytes);
        }
        result=native.result();
    }
    if(result.isEmpty()) {
        QCryptographicHash portable(QCryptographicHash::Sha256);
        if(!file.seek(0)||!portable.addData(&file))return {};
        result=portable.result();
    }
    // Neither an in-process nor a persisted entry may bless a concurrent Save.
    if(memoizable && neighborFileRevision(path)!=revision)return {};
    if(cache.size()>=512)cache.clear();
    if(memoizable){
        cache.insert(key,{revision,result});
        disk.setValue("revision",revision);disk.setValue("sha256",result);disk.sync();
    }
    return result;
}

QByteArray neighborContentFingerprint(const QString& path)
{
    const auto digest=neighborFileSha256(path);
    if(digest.isEmpty())return {};
    return QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toCaseFolded().toUtf8()+digest,QCryptographicHash::Sha256);
}

namespace {
void indexSections(const QVector<SMobSection>& sections,
                   QHash<QString, const SMobSection*>& index)
{
    for(const auto& section : sections)
    {
        index.insert(section.path, &section);
        indexSections(section.children, index);
    }
}
QByteArray leaf(const SMobSection& object, quint32 type)
{
    for(const auto& field : object.children)
        if(field.type == type) return field.rawBytes.mid(8);
    return {};
}
QString text(const QByteArray& bytes)
{
    QByteArray value = bytes;
    while(value.endsWith('\0')) value.chop(1);
    return QTextCodec::codecForName("Windows-1251")->toUnicode(value);
}
QJsonArray floats(const QByteArray& bytes, int count, const QJsonArray& fallback)
{
    if(bytes.size() != count * 4) return fallback;
    QJsonArray result;
    for(int i=0; i<count; ++i)
    {
        const quint32 bits=qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(bytes.constData()+4*i));
        float value; std::memcpy(&value,&bits,4);
        if(!std::isfinite(value)) return fallback;
        result.append(double(value));
    }
    return result;
}
}

QVector<QJsonObject> neighborStaticObjects(const CMobDocument& document)
{
    QHash<QString, const SMobSection*> sections;
    indexSections(document.sections(), sections);
    QVector<QJsonObject> result;
    for(const auto& object : document.objects())
    {
        // Ordinary world objects contain buildings, trees and rocks. Units,
        // lights, sounds, particles and gameplay helpers are not background geometry.
        if(object.unit || object.sectionType != 0xB001 || !object.hasPosition
           || object.model.isEmpty()) continue;
        const auto* section=sections.value(object.sectionPath,nullptr);
        if(!section) continue;
        const QJsonArray position=floats(leaf(*section,0xB009),3,{});
        if(position.size()!=3) continue;
        QJsonObject base;
        base["Map name"]=object.name;
        base["Position"]=position;
        base["Complection"]=floats(leaf(*section,0xB00C),3,{1,1,1});
        // MOB stores scalar first (w,x,y,z); object JSON / QVector4D stores
        // scalar last (x,y,z,w), as in CMobParser::readQuaternion.
        const auto rotation=floats(leaf(*section,0xB00A),4,{1,0,0,0});
        base["Rotation"]=QJsonArray{rotation[1],rotation[2],rotation[3],rotation[0]};
        QJsonObject visual;
        visual["Base object"]=base;
        visual["Model name"]=object.model;
        visual["Primary texture"]=text(leaf(*section,0xB007));
        visual["Secondary texture"]=text(leaf(*section,0xB008));
        visual["Object parts"]=QJsonArray::fromStringList(object.bodyParts);
        const QByteArray shadow=leaf(*section,0xB014);
        visual["Is shadow?"]=shadow.isEmpty() || shadow.at(0)!=0;
        result.append(visual);
    }
    return result;
}

SNeighborSceneData prepareNeighborScene(const SNeighborMapPlacement& placement,
                                        bool loadStaticObjects, bool loadTerrain)
{
    SNeighborSceneData result; result.placement=placement;
    if(loadTerrain) {
    CMprDocument terrain; CValidationReport report;
    if(!terrain.load(QFileInfo(placement.mprPath),&report) || report.hasErrors()
       || int(terrain.mapInfo().sectorCountX)*32 != qRound(placement.size.width())
       || int(terrain.mapInfo().sectorCountY)*32 != qRound(placement.size.height()))
    {
        result.diagnostics.append("Neighbor terrain rejected: "+placement.zoneId+" "+report.toText());
        return result;
    }
    for(const auto& member : terrain.members())
        result.terrainMembers.insert(member.name,member.payload);
    }
    result.valid=true;
    if(loadStaticObjects && !placement.mobPath.isEmpty())
    {
        CMobDocument mob; CValidationReport mobReport;
        if(mob.load(QFileInfo(placement.mobPath),&mobReport) && !mobReport.hasErrors())
            result.staticObjects=neighborStaticObjects(mob);
        else result.diagnostics.append("Neighbor objects rejected: "+placement.zoneId+" "+mobReport.toText());
    }
    return result;
}
