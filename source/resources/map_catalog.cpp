#include "map_catalog.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextCodec>
#include <stdexcept>

#include "res_archive_index.h"

namespace {
QString findNamedFile(const QString& directory, const QString& fileName)
{
    QDir dir(directory);
    if(!dir.exists())
        return QString();
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable);
    for(const QFileInfo& file : files)
        if(file.fileName().compare(fileName, Qt::CaseInsensitive) == 0)
            return file.absoluteFilePath();
    return QString();
}

QString resolvedMapPath(const QMap<QString,QString>& paths, QString name, const QString& extension)
{
    if(name.isEmpty())
        return QString();
    if(!name.endsWith(extension, Qt::CaseInsensitive))
        name += extension;
    return paths.value(name.toCaseFolded());
}
}

SMapCatalogResult CMapCatalog::load(const QVector<QString>& resourceRoots,
                                    const QVector<QString>& mapRoots)
{
    SMapCatalogResult result;
    QString textsArchive;
    for(const QString& root : resourceRoots)
    {
        textsArchive = findNamedFile(root, "texts.res");
        if(!textsArchive.isEmpty())
            break;
    }
    if(textsArchive.isEmpty())
    {
        result.messages.append("No texts.res was found in the active resource profile.");
        return result;
    }

    CResArchiveIndex archive;QString error;
    if(!archive.open(textsArchive,&error)) {
        result.messages.append(QString("Could not read %1: %2").arg(textsArchive,error));
        return result;
    }
    const QByteArray mapBytes=archive.readMember("map.txt",&error);
    if(mapBytes.isEmpty())
    {
        result.messages.append("The selected texts.res does not contain map.txt.");
        return result;
    }
    QTextCodec* cp1251 = QTextCodec::codecForName("CP1251");
    result.sourceArchive = textsArchive;
    result.topologyEntries = parseMapTopology(
        cp1251->toUnicode(mapBytes), textsArchive);
    QMap<QString,QString> paths;
    for(const auto& root:mapRoots)
        for(const auto& file:QDir(root).entryInfoList(QDir::Files|QDir::Readable,QDir::Name))
            if(!paths.contains(file.fileName().toCaseFolded()))
                paths.insert(file.fileName().toCaseFolded(),file.absoluteFilePath());
    for(SMapCatalogEntry& entry : result.topologyEntries)
    {
        entry.mprPath = resolvedMapPath(paths, entry.mprName, ".mpr");
        entry.mobPath = resolvedMapPath(paths, entry.mobName, ".mob");
        if(!entry.mprName.isEmpty() && !entry.mobName.isEmpty())
            result.entries.append(entry);
        if(!entry.mprName.isEmpty() && !entry.mobName.isEmpty()
           && !entry.isComplete())
            result.messages.append(QString("%1: missing %2%3")
                .arg(entry.zoneId,
                     entry.mprPath.isEmpty() ? entry.mprName + ".mpr" : QString(),
                     entry.mobPath.isEmpty()
                         ? QString("%1%2.mob").arg(entry.mprPath.isEmpty() ? " and " : "", entry.mobName)
                         : QString()));
    }
    if(result.entries.isEmpty())
        result.messages.append("map.txt did not contain any zones with an MPR/MOB resource pair.");
    return result;
}
