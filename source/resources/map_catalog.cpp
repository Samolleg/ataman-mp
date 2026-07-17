#include "map_catalog.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextCodec>
#include <stdexcept>

#include "res_file.h"

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

QString resolvedMapPath(const QVector<QString>& roots, QString name, const QString& extension)
{
    if(name.isEmpty())
        return QString();
    if(!name.endsWith(extension, Qt::CaseInsensitive))
        name += extension;
    for(const QString& root : roots)
    {
        const QString path = findNamedFile(root, name);
        if(!path.isEmpty())
            return path;
    }
    return QString();
}

QByteArray caseInsensitiveMember(const QMap<QString, QByteArray>& members,
                                 const QString& requested)
{
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
        if(it.key().compare(requested, Qt::CaseInsensitive) == 0)
            return it.value();
    return QByteArray();
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

    QByteArray mapBytes;
    try
    {
        CResFile archive(textsArchive);
        mapBytes = caseInsensitiveMember(archive.bufferOfFiles(), "map.txt");
    }
    catch(std::exception* error)
    {
        result.messages.append(QString("Could not read %1: %2")
            .arg(textsArchive, QString::fromLocal8Bit(error ? error->what() : "unknown RES error")));
        delete error;
        return result;
    }
    catch(const std::exception& error)
    {
        result.messages.append(QString("Could not read %1: %2")
            .arg(textsArchive, QString::fromLocal8Bit(error.what())));
        return result;
    }
    if(mapBytes.isEmpty())
    {
        result.messages.append("The selected texts.res does not contain map.txt.");
        return result;
    }
    QTextCodec* cp1251 = QTextCodec::codecForName("CP1251");
    result.sourceArchive = textsArchive;
    result.entries = parseMapTable(cp1251->toUnicode(mapBytes), textsArchive);
    for(SMapCatalogEntry& entry : result.entries)
    {
        entry.mprPath = resolvedMapPath(mapRoots, entry.mprName, ".mpr");
        entry.mobPath = resolvedMapPath(mapRoots, entry.mobName, ".mob");
        if(!entry.isComplete())
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
