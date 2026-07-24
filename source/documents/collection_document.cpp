#include "collection_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

namespace {
QString cleanValue(QString value)
{
    value = value.trimmed();
    if(value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
        value = value.mid(1, value.size() - 2);
    return value;
}

QString quoteValue(const QString& value)
{
    QString escaped = value;
    escaped.replace('"', "'");
    return '"' + escaped + '"';
}

QString findCaseInsensitive(const QString& root, const QString& reference)
{
    QFileInfo direct(QDir(root).filePath(reference));
    if(direct.exists() && direct.isFile())
        return direct.absoluteFilePath();
    const QFileInfo requested(reference);
    QDir directory(QDir(root).filePath(requested.path()));
    if(!directory.exists())
        return QString();
    for(const QFileInfo& candidate : directory.entryInfoList(QDir::Files | QDir::Readable))
        if(candidate.fileName().compare(requested.fileName(), Qt::CaseInsensitive) == 0)
            return candidate.absoluteFilePath();
    return QString();
}

QString resolveOne(const QString& reference, const QString& collectionDir,
                   const QVector<QString>& mapRoots)
{
    if(reference.trimmed().isEmpty())
        return QString();
    QFileInfo absolute(reference);
    if(absolute.isAbsolute() && absolute.exists() && absolute.isFile())
        return absolute.absoluteFilePath();
    QString found = findCaseInsensitive(collectionDir, reference);
    if(!found.isEmpty())
        return found;
    for(const QString& root : mapRoots)
    {
        found = findCaseInsensitive(root, reference);
        if(!found.isEmpty())
            return found;
    }
    return QString();
}
}

QString CCollectionDocument::defaultDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("collections");
}

bool CCollectionDocument::equivalent(const SEnduranceCollection& left,
                                     const SEnduranceCollection& right)
{
    auto normalizedReference = [](const QString& value) {
        return QDir::cleanPath(QDir::fromNativeSeparators(value.trimmed()));
    };
    if(left.version != right.version || left.name != right.name
       || left.profileId != right.profileId || left.mapZoneId != right.mapZoneId
       || normalizedReference(left.mprReference)
            != normalizedReference(right.mprReference)
       || left.mobReferences.size() != right.mobReferences.size())
        return false;
    for(int i = 0; i < left.mobReferences.size(); ++i)
        if(normalizedReference(left.mobReferences.at(i))
           != normalizedReference(right.mobReferences.at(i)))
            return false;
    return true;
}

bool CCollectionDocument::load(const QString& path, SEnduranceCollection* output,
                               QString* error)
{
    if(!output)
        return false;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if(error) *error = file.errorString();
        return false;
    }
    SEnduranceCollection result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    int lineNumber = 0;
    while(!stream.atEnd())
    {
        ++lineNumber;
        const QString line = stream.readLine().trimmed();
        if(line.isEmpty() || line.startsWith('#') || line.startsWith(';')
           || (line.startsWith('[') && line.endsWith(']')))
            continue;
        const int separator = line.indexOf('=');
        if(separator <= 0)
        {
            if(error) *error = QString("Line %1 is not key=value.").arg(lineNumber);
            return false;
        }
        const QString key = line.left(separator).trimmed().toLower();
        const QString value = cleanValue(line.mid(separator + 1));
        if(key == "version") result.version = value.toInt();
        else if(key == "name") result.name = value;
        else if(key == "profile") result.profileId = value;
        else if(key == "map") result.mapZoneId = value;
        else if(key == "mpr") result.mprReference = value;
        else if(key == "mob") result.mobReferences.append(value);
    }
    if(result.version != 1 || result.mprReference.isEmpty() || result.mobReferences.isEmpty())
    {
        if(error) *error = "Collection requires version=1, one mpr= line, and at least one mob= line.";
        return false;
    }
    if(result.name.isEmpty())
        result.name = QFileInfo(path).completeBaseName();
    *output = result;
    return true;
}

bool CCollectionDocument::save(const QString& path,
                               const SEnduranceCollection& collection,
                               QString* error)
{
    if(collection.mprReference.trimmed().isEmpty() || collection.mobReferences.isEmpty())
    {
        if(error) *error = "Collection requires one MPR and at least one MOB.";
        return false;
    }
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        if(error) *error = "Could not create the collection directory.";
        return false;
    }
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if(error) *error = file.errorString();
        return false;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << "# EI Mapper Endurance Collection\n"
           << "[collection]\n"
           << "version=1\n"
           << "name=" << quoteValue(collection.name) << "\n"
           << "profile=" << quoteValue(collection.profileId) << "\n"
           << "map=" << quoteValue(collection.mapZoneId) << "\n"
           << "mpr=" << quoteValue(collection.mprReference) << "\n";
    for(const QString& mob : collection.mobReferences)
        stream << "mob=" << quoteValue(mob) << "\n";
    stream.flush();
    if(stream.status() != QTextStream::Ok || !file.commit())
    {
        if(error) *error = file.errorString();
        return false;
    }
    return true;
}

SResolvedCollection CCollectionDocument::resolve(
    const SEnduranceCollection& collection, const QVector<QString>& mapRoots)
{
    SResolvedCollection result;
    result.document = collection;
    const QString directory = QFileInfo(collection.sourcePath).absolutePath();
    result.mprPath = resolveOne(collection.mprReference, directory, mapRoots);
    if(result.mprPath.isEmpty())
        result.errors.append("MPR: " + collection.mprReference);
    for(const QString& reference : collection.mobReferences)
    {
        const QString resolved = resolveOne(reference, directory, mapRoots);
        if(resolved.isEmpty()) result.errors.append("MOB: " + reference);
        else result.mobPaths.append(resolved);
    }
    return result;
}

QString CCollectionDocument::portableReference(
    const QString& absolutePath, const QString& collectionPath,
    const QVector<QString>& mapRoots)
{
    const QString clean = QFileInfo(absolutePath).absoluteFilePath();
    for(const QString& root : mapRoots)
    {
        const QString relative = QDir(root).relativeFilePath(clean);
        if(relative != ".." && !relative.startsWith("../") && !relative.startsWith("..\\"))
            return QDir::fromNativeSeparators(relative);
    }
    const QString relative = QFileInfo(collectionPath).absoluteDir().relativeFilePath(clean);
    if(relative != ".." && !relative.startsWith("../") && !relative.startsWith("..\\"))
        return QDir::fromNativeSeparators(relative);
    return QDir::fromNativeSeparators(clean);
}
