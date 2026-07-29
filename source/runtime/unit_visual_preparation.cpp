#include "unit_visual_preparation.h"
#include "profile_visual_provider.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>

namespace {

constexpr int kAppearanceCacheSchema = 1;
QMutex s_appearanceCacheMutex;
QHash<QString, QJsonObject> s_appearanceMemoryCache;

QJsonObject requestJson(const SUnitAppearancePreparationTask& task)
{
    QJsonObject request;
    request.insert(QStringLiteral("schema"), kAppearanceCacheSchema);
    request.insert(QStringLiteral("applicationVersion"), QCoreApplication::applicationVersion());
    request.insert(QStringLiteral("providerId"), task.providerId);
    request.insert(QStringLiteral("databaseCacheKey"),
                   task.database.isNull() ? QString() : task.database->cacheKey);
    request.insert(QStringLiteral("profileId"), task.request.profileId);
    request.insert(QStringLiteral("runtimeManifestId"), task.request.runtimeManifestId);
    request.insert(QStringLiteral("prototype"), task.request.prototype);
    request.insert(QStringLiteral("parentTemplate"), task.request.parentTemplate);
    request.insert(QStringLiteral("model"), task.request.model);
    request.insert(QStringLiteral("primaryTexture"), task.request.primaryTexture);
    request.insert(QStringLiteral("secondaryTexture"), task.request.secondaryTexture);
    request.insert(QStringLiteral("bodyParts"), QJsonArray::fromStringList(task.request.bodyParts));
    request.insert(QStringLiteral("dataSource"), int(task.request.dataSource));
    return request;
}

QString cachePath(const QString& directory, const QString& key)
{
    return QDir(directory).filePath(key.left(2) + QLatin1Char('/') + key + QStringLiteral(".json"));
}

bool canCacheTask(const SUnitAppearancePreparationTask& task)
{
    // A populated snapshot without a content key cannot be invalidated safely.
    // Null snapshots are valid for self-contained embedded-MOB appearance data.
    return task.database.isNull() || !task.database->cacheKey.isEmpty();
}

bool readCacheEntry(const QString& directory, const QString& key,
                    SUnitAppearancePreparationResult* result)
{
    if(directory.isEmpty() || !result)
        return false;
    const QString memoryKey = QDir::cleanPath(directory).toCaseFolded() + QLatin1Char('|') + key;
    QJsonObject root;
    {
        QMutexLocker locker(&s_appearanceCacheMutex);
        root = s_appearanceMemoryCache.value(memoryKey);
    }
    if(root.isEmpty())
    {
        QFile file(cachePath(directory, key));
        if(!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 1024 * 1024)
            return false;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if(error.error != QJsonParseError::NoError)
            return false;
        root = document.object();
        QMutexLocker locker(&s_appearanceCacheMutex);
        s_appearanceMemoryCache.insert(memoryKey, root);
    }
    if(root.value(QStringLiteral("schema")).toInt() != kAppearanceCacheSchema
            || root.value(QStringLiteral("key")).toString() != key)
        return false;
    const QJsonObject provider = root.value(QStringLiteral("providerResult")).toObject();
    if(!provider.value(QStringLiteral("success")).toBool())
        return false;
    result->providerResult.success = true;
    result->providerResult.code = provider.value(QStringLiteral("code")).toString();
    result->providerResult.message = provider.value(QStringLiteral("message")).toString();
    result->providerResult.evidence = provider.value(QStringLiteral("evidence")).toObject();
    result->appearance = root.value(QStringLiteral("appearance")).toObject();
    result->diagnostic = root.value(QStringLiteral("diagnostic")).toString();
    result->cacheHit = true;
    return true;
}

void writeCacheEntry(const QString& directory, const QString& key,
                     const SUnitAppearancePreparationResult& result)
{
    if(directory.isEmpty() || !result.providerResult.success)
        return;
    const QFileInfo target(cachePath(directory, key));
    if(!QDir().mkpath(target.absolutePath()))
        return;
    QJsonObject provider;
    provider.insert(QStringLiteral("success"), true);
    provider.insert(QStringLiteral("code"), result.providerResult.code);
    provider.insert(QStringLiteral("message"), result.providerResult.message);
    provider.insert(QStringLiteral("evidence"), result.providerResult.evidence);
    QJsonObject root;
    root.insert(QStringLiteral("schema"), kAppearanceCacheSchema);
    root.insert(QStringLiteral("key"), key);
    root.insert(QStringLiteral("providerResult"), provider);
    root.insert(QStringLiteral("appearance"), result.appearance);
    root.insert(QStringLiteral("diagnostic"), result.diagnostic);
    QSaveFile file(target.absoluteFilePath());
    if(file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        if(file.commit())
        {
            const QString memoryKey = QDir::cleanPath(directory).toCaseFolded()
                + QLatin1Char('|') + key;
            QMutexLocker locker(&s_appearanceCacheMutex);
            s_appearanceMemoryCache.insert(memoryKey, root);
        }
    }
}

}

QString unitAppearancePreparationCacheKey(const SUnitAppearancePreparationTask& task)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(requestJson(task)).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}

QString unitAppearancePreparationCacheDirectory()
{
    return unitAppearancePreparationCacheDirectoryForApplication(
        QCoreApplication::applicationDirPath());
}

QString unitAppearancePreparationCacheDirectoryForApplication(
    const QString& applicationDirectory)
{
    if(applicationDirectory.trimmed().isEmpty())
        return QString();
    return QDir(applicationDirectory).filePath(
        QStringLiteral("cache/unit-appearances-v1"));
}

QVector<SUnitAppearancePreparationResult> resolveUnitAppearanceTasks(
    const QVector<SUnitAppearancePreparationTask>& tasks, const QString& cacheDirectory)
{
    QVector<SUnitAppearancePreparationResult> results;
    results.reserve(tasks.size());
    ProfileVisualProviderService providers;
    const QString effectiveCacheDirectory = cacheDirectory.isNull()
        ? unitAppearancePreparationCacheDirectory() : cacheDirectory;
    for(const SUnitAppearancePreparationTask& task : tasks)
    {
        SUnitAppearancePreparationResult result;
        result.unitKey = task.unitKey;
        result.databaseGeneration = task.databaseGeneration;
        result.visualRevision = task.visualRevision;
        const QString cacheKey = unitAppearancePreparationCacheKey(task);
        const bool cacheable = canCacheTask(task);
        if(cacheable && readCacheEntry(effectiveCacheDirectory, cacheKey, &result))
        {
            results.append(result);
            continue;
        }
        const auto resolver = providers.appearanceResolver(
            task.providerId, task.database, &result.diagnostic);
        if(resolver.isNull())
        {
            result.providerResult.code = QStringLiteral("provider.unavailable");
            result.providerResult.message = result.diagnostic;
        }
        else
            result.providerResult = resolver->resolve(task.request, &result.appearance);
        if(cacheable)
            writeCacheEntry(effectiveCacheDirectory, cacheKey, result);
        results.append(result);
    }
    return results;
}
