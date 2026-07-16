#include "resource_profile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStorageInfo>

namespace {
QStringList jsonStrings(const QJsonObject& object, const char* key)
{
    QStringList result;
    for(const QJsonValue& value : object.value(key).toArray())
        if(value.isString())
            result.append(value.toString());
    return result;
}

bool profileFromJson(const QByteArray& bytes, const QString& source,
                     SResourceProfile* profile, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if(parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if(error)
            *error = parseError.errorString();
        return false;
    }
    const QJsonObject object = document.object();
    if(object.value("schemaVersion").toInt() != 1)
    {
        if(error)
            *error = "Unsupported or missing schemaVersion";
        return false;
    }

    SResourceProfile value;
    value.id = object.value("id").toString().trimmed();
    value.displayName = object.value("displayName").toString().trimmed();
    value.source = source;
    value.fixedModRoots = jsonStrings(object, "fixedModRoots");
    value.searchParent = object.value("searchParent").toString();
    value.directoryNameContains = jsonStrings(object, "directoryNameContains");
    value.requiredFiles = jsonStrings(object, "requiredFiles");
    value.requiredDirectories = jsonStrings(object, "requiredDirectories");
    value.signatureFiles = jsonStrings(object, "signatureFiles");
    value.resourceDirectories = jsonStrings(object, "resourceDirectories");
    value.mapDirectories = jsonStrings(object, "mapDirectories");
    if(value.id.isEmpty() || value.displayName.isEmpty() || value.resourceDirectories.isEmpty())
    {
        if(error)
            *error = "A profile requires id, displayName, and resourceDirectories";
        return false;
    }
    *profile = value;
    return true;
}

QString absoluteFromRoot(const QString& root, const QString& relative)
{
    if(QDir::isAbsolutePath(relative))
        return QDir::cleanPath(relative);
    return QDir::cleanPath(QDir(root).absoluteFilePath(relative));
}

QString expandPath(QString value, const QString& gameRoot, const QString& modRoot)
{
    value.replace("{gameRoot}", gameRoot, Qt::CaseInsensitive);
    value.replace("{modRoot}", modRoot, Qt::CaseInsensitive);
    return QDir::cleanPath(value);
}

bool candidateMeetsRequirements(const QString& candidate, const SResourceProfile& profile)
{
    if(candidate.isEmpty() || !QDir(candidate).exists() ||
       QFileInfo(candidate).fileName().contains("backup", Qt::CaseInsensitive))
        return false;
    for(const QString& file : profile.requiredFiles)
        if(!QFileInfo(absoluteFromRoot(candidate, file)).isFile())
            return false;
    for(const QString& directory : profile.requiredDirectories)
        if(!QFileInfo(absoluteFromRoot(candidate, directory)).isDir())
            return false;
    return true;
}

int candidateScore(const QString& candidate, const SResourceProfile& profile)
{
    int score = 0;
    const QString name = QFileInfo(candidate).fileName();
    for(const QString& text : profile.directoryNameContains)
        if(name.contains(text, Qt::CaseInsensitive))
            score += 10;
    for(const QString& signature : profile.signatureFiles)
        if(QFileInfo(absoluteFromRoot(candidate, signature)).isFile())
            score += 100;
    return score;
}

void appendUnique(QStringList* values, const QString& path)
{
    const QString clean = QDir::cleanPath(path);
    for(const QString& existing : *values)
        if(existing.compare(clean, Qt::CaseInsensitive) == 0)
            return;
    values->append(clean);
}
}

QVector<SResourceProfile> builtInResourceProfiles()
{
    SResourceProfile jmv;
    jmv.id = "jamais-vu";
    jmv.displayName = "Jamais Vu (JMV)";
    jmv.source = "built-in";
    jmv.fixedModRoots = QStringList() << "Starter/MODS/JamaisVu135";
    jmv.searchParent = "Starter/MODS";
    jmv.directoryNameContains = QStringList() << "JamaisVu";
    jmv.requiredFiles = QStringList() << "main.lua" << "addon.dll";
    jmv.requiredDirectories = QStringList() << "res" << "Maps";
    jmv.signatureFiles = QStringList() << "res/databaseJmv.res";
    jmv.resourceDirectories = QStringList() << "{modRoot}/res" << "{gameRoot}/Res";
    jmv.mapDirectories = QStringList() << "{modRoot}/Maps" << "{gameRoot}/Maps";

    SResourceProfile base;
    base.id = "base-game";
    base.displayName = "Base game only";
    base.source = "built-in";
    base.resourceDirectories = QStringList() << "{gameRoot}/Res";
    base.mapDirectories = QStringList() << "{gameRoot}/Maps";

    return {jmv, base};
}

QVector<SResourceProfile> loadResourceProfiles(const QString& externalDirectory)
{
    QVector<SResourceProfile> profiles = builtInResourceProfiles();
    const QString directoryPath = externalDirectory.isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()).filePath("resource-profiles")
        : externalDirectory;
    const QDir directory(directoryPath);
    for(const QFileInfo& file : directory.entryInfoList({"*.json"}, QDir::Files, QDir::Name))
    {
        QFile input(file.absoluteFilePath());
        if(!input.open(QIODevice::ReadOnly))
            continue;
        SResourceProfile profile;
        if(!profileFromJson(input.readAll(), file.absoluteFilePath(), &profile, nullptr))
            continue;
        bool replaced = false;
        for(SResourceProfile& existing : profiles)
        {
            if(existing.id.compare(profile.id, Qt::CaseInsensitive) == 0)
            {
                existing = profile;
                replaced = true;
                break;
            }
        }
        if(!replaced)
            profiles.append(profile);
    }
    return profiles;
}

SResourceProfileEvaluation evaluateResourceProfile(const QString& gameRoot,
                                                   const SResourceProfile& profile)
{
    SResourceProfileEvaluation result;
    result.gameRoot = QDir::cleanPath(QFileInfo(gameRoot).absoluteFilePath());
    result.profileId = profile.id;
    result.profileName = profile.displayName;
    if(!QFileInfo(QDir(result.gameRoot).filePath("game.exe")).isFile())
    {
        result.messages.append("The selected folder does not contain game.exe.");
        return result;
    }

    const bool needsModRoot = !profile.fixedModRoots.isEmpty() || !profile.searchParent.isEmpty();
    if(needsModRoot)
    {
        QStringList candidates;
        for(const QString& relative : profile.fixedModRoots)
            appendUnique(&candidates, absoluteFromRoot(result.gameRoot, relative));
        const QDir parent(absoluteFromRoot(result.gameRoot, profile.searchParent));
        for(const QFileInfo& directory : parent.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            appendUnique(&candidates, directory.absoluteFilePath());

        int bestScore = -1;
        for(const QString& candidate : candidates)
        {
            if(!candidateMeetsRequirements(candidate, profile))
                continue;
            const int score = candidateScore(candidate, profile);
            if(score > bestScore)
            {
                result.modRoot = candidate;
                bestScore = score;
            }
        }
        if(result.modRoot.isEmpty())
        {
            result.messages.append(QString("%1 was not found below the selected game installation.")
                                   .arg(profile.displayName));
            return result;
        }
    }

    for(const QString& path : profile.resourceDirectories)
    {
        const QString expanded = expandPath(path, result.gameRoot, result.modRoot);
        const QDir directory(expanded);
        if(!directory.exists())
        {
            result.messages.append("Missing resource directory: " + expanded);
            continue;
        }
        const int count = directory.entryList({"*.res"}, QDir::Files, QDir::Name | QDir::IgnoreCase).size();
        if(count == 0)
        {
            result.messages.append("No RES archives found in: " + expanded);
            continue;
        }
        result.resourceDirectories.append(expanded);
        result.archiveCount += count;
    }
    for(const QString& path : profile.mapDirectories)
    {
        const QString expanded = expandPath(path, result.gameRoot, result.modRoot);
        if(QDir(expanded).exists())
            result.mapDirectories.append(expanded);
        else
            result.messages.append("Optional map directory not found: " + expanded);
    }
    result.valid = result.resourceDirectories.size() == profile.resourceDirectories.size() &&
                   result.archiveCount > 0;
    return result;
}

QStringList discoverGameInstallations()
{
    QStringList result;
    QStringList candidates;
    for(const QStorageInfo& storage : QStorageInfo::mountedVolumes())
    {
        if(!storage.isValid() || !storage.isReady())
            continue;
        const QString root = storage.rootPath();
        candidates << QDir(root).filePath("Evil Islands")
                   << QDir(root).filePath("GOG Games/Evil Islands")
                   << QDir(root).filePath("Games/Evil Islands");
    }
    for(const QString& candidate : candidates)
    {
        if(QFileInfo(QDir(candidate).filePath("game.exe")).isFile())
            appendUnique(&result, QFileInfo(candidate).absoluteFilePath());
    }
    return result;
}
