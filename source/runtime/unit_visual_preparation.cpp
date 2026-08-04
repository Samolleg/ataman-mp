#include "unit_visual_preparation.h"
#include "profile_visual_provider.h"
#include "persistent_visual_cache.h"

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
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

namespace {

constexpr int kAppearanceCacheSchema = 1;
QMutex s_appearanceCacheMutex;
QHash<QString, QJsonObject> s_appearanceMemoryCache;
QSet<QString> s_cleanedLegacyCacheRoots;
const QString kAppearanceCacheManifest = QStringLiteral("cache-manifest.json");

bool writeCacheManifest(const QString& directory)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), kAppearanceCacheSchema);
    QSaveFile file(QDir(directory).filePath(kAppearanceCacheManifest));
    if(!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

bool prepareCacheDirectory(const QString& directory)
{
    if(directory.isEmpty())
        return false;
    QMutexLocker locker(&s_appearanceCacheMutex);
    QDir cache(directory);
    bool compatible = false;
    if(cache.exists())
    {
        QFile manifest(cache.filePath(kAppearanceCacheManifest));
        if(manifest.open(QIODevice::ReadOnly) && manifest.size() > 0
                && manifest.size() <= 4096)
        {
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &error);
            compatible = error.error == QJsonParseError::NoError
                && document.isObject()
                && document.object().value(QStringLiteral("schema")).toInt(-1)
                    == kAppearanceCacheSchema;
        }
        if(!compatible)
        {
            manifest.close();
            if(!cache.removeRecursively())
                return false;
            s_appearanceMemoryCache.clear();
        }
    }
    if(!QDir().mkpath(directory))
        return false;
    return compatible || writeCacheManifest(directory);
}

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
        const QString path = cachePath(directory, key);
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 1024 * 1024)
        {
            file.close();
            QFile::remove(path);
            return false;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if(error.error != QJsonParseError::NoError)
        {
            file.close();
            QFile::remove(path);
            return false;
        }
        root = document.object();
        QMutexLocker locker(&s_appearanceCacheMutex);
        s_appearanceMemoryCache.insert(memoryKey, root);
    }
    if(root.value(QStringLiteral("schema")).toInt() != kAppearanceCacheSchema
            || root.value(QStringLiteral("key")).toString() != key)
    {
        QFile::remove(cachePath(directory, key));
        QMutexLocker locker(&s_appearanceCacheMutex);
        s_appearanceMemoryCache.remove(memoryKey);
        return false;
    }
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
    const qint64 previousSize = target.size();
    QSaveFile file(target.absoluteFilePath());
    if(file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        if(file.commit())
        {
            ei::recordVisualCacheFileWrite(directory, previousSize,
                                           QFileInfo(target.absoluteFilePath()).size());
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

QVector<SUnitAppearancePreparationTask> selectUnitAppearancePreparationBatch(
    const QVector<SUnitAppearancePreparationTask>& distanceOrderedTasks,
    int maximumUniqueAppearances)
{
    QVector<SUnitAppearancePreparationTask> selected;
    if(maximumUniqueAppearances <= 0)
        return selected;
    QSet<QString> selectedKeys;
    for(const SUnitAppearancePreparationTask& task : distanceOrderedTasks)
    {
        QString key = unitAppearancePreparationCacheKey(task);
        if(!canCacheTask(task))
            key += QStringLiteral("|unit:") + QString::number(qulonglong(task.unitKey));
        if(!selectedKeys.contains(key))
        {
            if(selectedKeys.size() >= maximumUniqueAppearances)
                continue;
            selectedKeys.insert(key);
        }
        selected.append(task);
    }
    return selected;
}

int unitAppearancePreparationWorkerLimit(int idealThreadCount)
{
    return qBound(1, idealThreadCount, 4);
}

QString unitAppearancePreparationCacheDirectory()
{
    if(!QCoreApplication::instance())
        return QString();
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const QString directory = unitAppearancePreparationCacheDirectoryForApplication(
        applicationDirectory);
    const QString cacheRoot = QDir(applicationDirectory).filePath(QStringLiteral("cache"));
    {
        QMutexLocker locker(&s_appearanceCacheMutex);
        const QString normalizedRoot = QDir::cleanPath(cacheRoot).toCaseFolded();
        if(!s_cleanedLegacyCacheRoots.contains(normalizedRoot))
        {
            QDir root(cacheRoot);
            for(const QString& legacy : root.entryList(
                    QStringList() << QStringLiteral("unit-appearances-v*"),
                    QDir::Dirs | QDir::NoDotAndDotDot))
                QDir(root.filePath(legacy)).removeRecursively();
            s_cleanedLegacyCacheRoots.insert(normalizedRoot);
        }
    }
    return directory;
}

QString unitAppearancePreparationCacheDirectoryForApplication(
    const QString& applicationDirectory)
{
    if(applicationDirectory.trimmed().isEmpty())
        return QString();
    return QDir(applicationDirectory).filePath(
        QStringLiteral("cache/unit-appearances"));
}

QVector<SUnitAppearancePreparationResult> resolveUnitAppearanceTasks(
    const QVector<SUnitAppearancePreparationTask>& tasks, const QString& cacheDirectory)
{
    QVector<SUnitAppearancePreparationResult> results(tasks.size());
    if(tasks.isEmpty())
        return results;
    const QString effectiveCacheDirectory = cacheDirectory.isNull()
        ? unitAppearancePreparationCacheDirectory() : cacheDirectory;
    const bool cacheReady = prepareCacheDirectory(effectiveCacheDirectory);

    struct SPreparationGroup
    {
        QString cacheKey;
        SUnitAppearancePreparationTask representative;
        QVector<int> taskIndexes;
        bool cacheable{false};
    };
    QVector<SPreparationGroup> groups;
    QHash<QString, int> groupByKey;
    for(int taskIndex = 0; taskIndex < tasks.size(); ++taskIndex)
    {
        const SUnitAppearancePreparationTask& task = tasks.at(taskIndex);
        const QString cacheKey = unitAppearancePreparationCacheKey(task);
        const bool cacheable = canCacheTask(task);
        int groupIndex = cacheable ? groupByKey.value(cacheKey, -1) : -1;
        if(groupIndex < 0)
        {
            groupIndex = groups.size();
            SPreparationGroup group;
            group.cacheKey = cacheKey;
            group.representative = task;
            group.cacheable = cacheable;
            groups.append(group);
            if(cacheable)
                groupByKey.insert(cacheKey, groupIndex);
        }
        groups[groupIndex].taskIndexes.append(taskIndex);
    }

    QThreadPool workerPool;
    workerPool.setMaxThreadCount(unitAppearancePreparationWorkerLimit(
        QThread::idealThreadCount()));
    QVector<QFuture<SUnitAppearancePreparationResult>> futures;
    futures.reserve(groups.size());
    for(const SPreparationGroup& group : groups)
    {
        const SUnitAppearancePreparationTask task = group.representative;
        const QString cacheKey = group.cacheKey;
        const bool cacheable = group.cacheable;
        futures.append(QtConcurrent::run(&workerPool,
            [task, cacheKey, cacheable, cacheReady, effectiveCacheDirectory]()
        {
            SUnitAppearancePreparationResult result;
            result.unitKey = task.unitKey;
            result.databaseGeneration = task.databaseGeneration;
            result.visualRevision = task.visualRevision;
            if(cacheable && cacheReady
                    && readCacheEntry(effectiveCacheDirectory, cacheKey, &result))
                return result;
            ProfileVisualProviderService providers;
            const auto resolver = providers.appearanceResolver(
                task.providerId, task.database, &result.diagnostic);
            if(resolver.isNull())
            {
                result.providerResult.code = QStringLiteral("provider.unavailable");
                result.providerResult.message = result.diagnostic;
            }
            else
            {
                result.providerExecuted = true;
                result.providerResult = resolver->resolve(task.request, &result.appearance);
            }
            if(cacheable && cacheReady)
                writeCacheEntry(effectiveCacheDirectory, cacheKey, result);
            return result;
        }));
    }

    for(int groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
    {
        const SUnitAppearancePreparationResult prepared = futures.at(groupIndex).result();
        const SPreparationGroup& group = groups.at(groupIndex);
        for(int memberIndex = 0; memberIndex < group.taskIndexes.size(); ++memberIndex)
        {
            const int taskIndex = group.taskIndexes.at(memberIndex);
            const SUnitAppearancePreparationTask& task = tasks.at(taskIndex);
            SUnitAppearancePreparationResult result = prepared;
            result.unitKey = task.unitKey;
            result.databaseGeneration = task.databaseGeneration;
            result.visualRevision = task.visualRevision;
            if(memberIndex > 0)
            {
                result.cacheHit = false;
                result.providerExecuted = false;
                result.sharedPreparation = true;
            }
            results[taskIndex] = result;
        }
    }
    return results;
}
