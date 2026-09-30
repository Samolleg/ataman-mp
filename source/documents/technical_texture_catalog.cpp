#include "technical_texture_catalog.h"

#include "endurance_document.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QJsonArray>
#include <algorithm>

namespace ei {
namespace {

QString normalizedName(QString value, const QString& extension)
{
    value = value.trimmed().toLower();
    if(value.endsWith(extension, Qt::CaseInsensitive))
        value.chop(extension.size());
    return value;
}

} // namespace

QJsonObject TechnicalTextureCatalog::saveCache() const
{
    QJsonArray files;
    for(const SourceFile& source : m_sources)
    {
        const auto found = m_results.constFind(source.path);
        if(found == m_results.constEnd()) continue;
        QJsonObject pairs;
        for(auto model = found->pairs.constBegin(); model != found->pairs.constEnd(); ++model)
        {
            QJsonObject textures;
            for(auto texture = model->constBegin(); texture != model->constEnd(); ++texture)
                textures.insert(texture.key(), texture.value());
            pairs.insert(model.key(), textures);
        }
        files.append(QJsonObject{{"path", source.path}, {"size", QString::number(source.size)},
            {"modified", QString::number(source.modified.toMSecsSinceEpoch())},
            {"loaded", found->loaded}, {"pairs", pairs}});
    }
    return QJsonObject{{"files", files}};
}

void TechnicalTextureCatalog::restoreCache(const QJsonObject& cache)
{
    m_results.clear(); m_pending.clear(); m_sources.clear();
    const QJsonArray files = cache.value("files").toArray();
    if(files.size() > 20000) return;
    for(const QJsonValue& value : files)
    {
        const QJsonObject file = value.toObject();
        FileResult result;
        result.source.path = file.value("path").toString();
        bool sizeOk = false, timeOk = false;
        result.source.size = file.value("size").toString().toLongLong(&sizeOk);
        const qint64 modified = file.value("modified").toString().toLongLong(&timeOk);
        if(result.source.path.isEmpty() || !sizeOk || !timeOk || result.source.size < 0) continue;
        result.source.modified = QDateTime::fromMSecsSinceEpoch(modified);
        result.loaded = file.value("loaded").toBool();
        const QJsonObject pairs = file.value("pairs").toObject();
        for(auto model = pairs.constBegin(); model != pairs.constEnd(); ++model)
        {
            const QJsonObject textures = model.value().toObject();
            for(auto texture = textures.constBegin(); texture != textures.constEnd(); ++texture)
                if(texture.value().toInt() > 0)
                    result.pairs[model.key()][texture.key()] = texture.value().toInt();
        }
        m_results.insert(result.source.path, result);
    }
}

QMap<QString, QStringList> TechnicalTextureCatalog::observedModelTextures() const
{
    QMap<QString, QHash<QString, int>> totals;
    for(const SourceFile& source : m_sources)
    {
        const auto file = m_results.constFind(source.path);
        if(file == m_results.constEnd() || !file->loaded) continue;
        for(auto model = file->pairs.constBegin(); model != file->pairs.constEnd(); ++model)
            for(auto texture = model->constBegin(); texture != model->constEnd(); ++texture)
                totals[model.key()][texture.key()] += texture.value();
    }
    QMap<QString, QStringList> result;
    for(auto model = totals.constBegin(); model != totals.constEnd(); ++model)
    {
        QStringList names = model->keys();
        std::sort(names.begin(), names.end(), [&model](const QString& a, const QString& b) {
            return model->value(a) == model->value(b) ? a < b : model->value(a) > model->value(b);
        });
        result.insert(model.key(), names);
    }
    return result;
}

void TechnicalTextureCatalog::refreshSources(const QStringList& mapRoots)
{
    QVector<SourceFile> sources;
    QSet<QString> logicalNames;
    QSet<QString> paths;
    for(const QString& root : mapRoots)
    {
        QStringList candidates;
        QDirIterator it(root, QStringList{QStringLiteral("*.mob")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while(it.hasNext())
            candidates.append(it.next());
        candidates.sort(Qt::CaseInsensitive);
        for(const QString& candidate : candidates)
        {
            const QFileInfo info(candidate);
            const QString path = info.absoluteFilePath();
            const QString pathKey = path.toLower();
            const QString logicalName = info.fileName().toLower();
            // Earlier map roots have the same priority as the actual map loader.
            if(paths.contains(pathKey) || logicalNames.contains(logicalName))
                continue;
            paths.insert(pathKey);
            logicalNames.insert(logicalName);
            SourceFile source;
            source.path = path;
            source.size = info.size();
            source.modified = info.lastModified();
            sources.append(source);
        }
    }
    std::sort(sources.begin(), sources.end(), [](const SourceFile& left, const SourceFile& right) {
        return left.path.compare(right.path, Qt::CaseInsensitive) < 0;
    });
    m_sources = sources;
    m_pending.clear();
    for(auto result = m_results.begin(); result != m_results.end();)
    {
        if(!paths.contains(result.key().toLower()))
            result = m_results.erase(result);
        else
            ++result;
    }
    for(const SourceFile& source : m_sources)
    {
        const auto cached = m_results.constFind(source.path);
        if(cached == m_results.constEnd() || cached->source.size != source.size
            || cached->source.modified != source.modified)
            m_pending.append(source.path);
    }
}

void TechnicalTextureCatalog::scanNextFile()
{
    if(m_pending.isEmpty())
        return;
    const QString path = m_pending.takeFirst();
    const auto source = std::find_if(m_sources.constBegin(), m_sources.constEnd(),
        [&path](const SourceFile& item) { return item.path == path; });
    if(source == m_sources.constEnd())
        return;

    FileResult result;
    result.source = *source;
    CMobDocument document;
    result.loaded = document.load(QFileInfo(path));
    if(result.loaded)
    {
        for(const SMobObjectSnapshot& object : document.objects())
        {
            const QString model = normalizedName(object.model, QStringLiteral(".mod"));
            const QString texture = normalizedName(object.primaryTexture, QStringLiteral(".mmp"));
            if(!model.isEmpty() && !texture.isEmpty())
                ++result.pairs[model][texture];
        }
    }
    m_results.insert(path, result);
}

int TechnicalTextureCatalog::failedFileCount() const
{
    int count = 0;
    for(const SourceFile& source : m_sources)
    {
        const auto result = m_results.constFind(source.path);
        if(result != m_results.constEnd() && !result->loaded)
            ++count;
    }
    return count;
}

QVector<STechnicalTextureChoice> TechnicalTextureCatalog::exactChoices(
    const QString& model, const QStringList& available) const
{
    const QString modelKey = normalizedName(model, QStringLiteral(".mod"));
    if(modelKey.isEmpty())
        return {};

    QHash<QString, QString> availableNames;
    for(const QString& name : available)
    {
        const QString key = normalizedName(name, QStringLiteral(".mmp"));
        if(!key.isEmpty() && !availableNames.contains(key))
            availableNames.insert(key, name);
    }
    QHash<QString, int> totals;
    for(const SourceFile& source : m_sources)
    {
        const auto result = m_results.constFind(source.path);
        if(result == m_results.constEnd() || !result->loaded)
            continue;
        const auto modelPairs = result->pairs.constFind(modelKey);
        if(modelPairs == result->pairs.constEnd())
            continue;
        for(auto texture = modelPairs->constBegin(); texture != modelPairs->constEnd(); ++texture)
            if(availableNames.contains(texture.key()))
                totals[texture.key()] += texture.value();
    }
    QVector<STechnicalTextureChoice> choices;
    for(auto it = totals.constBegin(); it != totals.constEnd(); ++it)
    {
        STechnicalTextureChoice choice;
        choice.name = availableNames.value(it.key());
        choice.observedCount = it.value();
        choices.append(choice);
    }
    std::sort(choices.begin(), choices.end(), [](const STechnicalTextureChoice& left,
                                                   const STechnicalTextureChoice& right) {
        if(left.observedCount != right.observedCount)
            return left.observedCount > right.observedCount;
        return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
    });
    return choices;
}

} // namespace ei
