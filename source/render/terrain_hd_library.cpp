#include "terrain_hd_library.h"
#include "terrain_hd_progress.h"
#include "res_archive_index.h"
#include "res_archive_document.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QLockFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtEndian>

namespace TerrainHdLibrary {
QString root() {
    const auto override=qEnvironmentVariable("EI_MAPPER_HD_LIBRARY");
    return override.isEmpty()?QDir(QCoreApplication::applicationDirPath()).filePath("hd-library"):override;
}
QStringList configuredSources(const QStringList& archives) {
    QStringList result=archives;
    for(const auto& path:archives) {
        QDir res=QFileInfo(path).dir();QDir mod=res;mod.cdUp();
        if(mod.dirName().compare("Dejavu",Qt::CaseInsensitive)==0) {
            const QString fallback=res.filePath("../../JamaisVu135/res/HDPack_Terrain.res");
            if(QFileInfo::exists(fallback))result.append(QFileInfo(fallback).absoluteFilePath());
        }
    }
    result.removeDuplicates();return result;
}
bool ensure(const QStringList& paths,QStringList* output,QString* error,CTerrainHdProgress* progress,const QString& directory) {
    const QString target=directory.isEmpty()?root():directory;
    auto fail=[&](const QString& message){if(error)*error=message;return false;};
    auto read=[&](const QString& path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();};
    auto load=[&]() {
        const auto manifest=QJsonDocument::fromJson(read(QDir(target).filePath("sources/manifest.json"))).object();
        if(manifest["schema"].toInt()!=1)return fail(QStringLiteral("Повреждён каталог HD-библиотеки: %1").arg(target));
        QStringList local;
        for(const auto& value:manifest["archives"].toArray()) {
            const auto entry=value.toObject();const auto name=entry["file"].toString();
            if(!QRegularExpression("^source-[0-9]+(-dejavu)?\\.res$").match(name).hasMatch())return fail("Invalid HD library file");
            const QString path=QDir(target).filePath("sources/"+name);
            if(!QFileInfo(path).isFile()||QFileInfo(path).size()!=qint64(entry["bytes"].toDouble()))return fail(QStringLiteral("Отсутствует или повреждён источник HD: %1").arg(path));
            local.append(path);
        }
        if(local.isEmpty())return fail("Empty HD library");
        *output=local;return true;
    };
    if(QFileInfo::exists(QDir(target).filePath("sources")))return load();
    if(!QDir().mkpath(target))return fail(QStringLiteral("Не удалось создать HD-библиотеку: %1").arg(target));
    QLockFile lock(QDir(target).filePath("import.lock"));
    if(!lock.tryLock(0))return fail(QStringLiteral("HD-библиотека уже импортируется другим процессом."));
    if(QFileInfo::exists(QDir(target).filePath("sources")))return load();
    QTemporaryDir staging(QDir(target).filePath("import-XXXXXX"));
    if(!staging.isValid())return fail("Cannot stage HD library");
    QJsonArray entries;int index=0;
    const QRegularExpression page("^(.+?)([0-9]{3})\\.mmp$",QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression published("_[0-9a-f]{12}$");
    for(const auto& path:paths) {
        if(progress&&!progress->update(CTerrainHdProgress::Opening,index,paths.size()))return fail("Cancelled");
        CResArchiveIndex source;if(!source.open(path,error))return false;
        QVector<SResArchiveNewMember> members;
        for(const auto& member:source.members()) {
            if(progress&&progress->cancelled())return fail("Cancelled");
            const auto match=page.match(member.name);
            const bool profile=member.name.startsWith("ei_atlas_profile_")&&member.name.endsWith(".json");
            if(!profile&&(!match.hasMatch()||published.match(match.captured(1)).hasMatch()))continue;
            const auto payload=source.readMember(member.name,error);
            if(payload.isEmpty())return fail("Cannot import terrain member: "+member.name);
            if(!profile) {
                if(payload.size()<76)continue;
                const auto data=reinterpret_cast<const uchar*>(payload.constData());
                const auto width=qFromLittleEndian<quint32>(data+4);
                if((width!=1024&&width!=2048)||qFromLittleEndian<quint32>(data+8)!=width||qFromLittleEndian<quint32>(data+16)!=0x31545844)continue;
            }
            SResArchiveNewMember copy;copy.name=member.name;copy.payload=payload;members.append(copy);
        }
        if(!source.sourceIsCurrent(error))return false;
        ++index;if(members.isEmpty())continue;
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QByteArray bytes;QStringList errors;
        if(!archive.loadBytes(empty)||!archive.buildWithAppendedMembers(members,&bytes,&errors))return fail(errors.join('\n'));
        const QString name=QStringLiteral("source-%1%2.res").arg(entries.size()).arg(path.contains("dejavu",Qt::CaseInsensitive)?"-dejavu":"");
        QSaveFile file(staging.filePath(name));if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return fail(file.errorString());
        entries.append(QJsonObject{{"file",name},{"origin",path},{"bytes",double(bytes.size())},{"sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}});
    }
    if(entries.isEmpty())return fail("No terrain sources to import");
    QSaveFile manifest(staging.filePath("manifest.json"));const auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"archives",entries}}).toJson();
    if(!manifest.open(QIODevice::WriteOnly)||manifest.write(bytes)!=bytes.size()||!manifest.commit())return fail(manifest.errorString());
    if(progress&&progress->cancelled())return fail("Cancelled");
    if(!QDir().rename(staging.path(),QDir(target).filePath("sources")))return fail("Cannot publish HD library");
    staging.setAutoRemove(false);return load();
}
}
