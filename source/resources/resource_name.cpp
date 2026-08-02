#include "resource_name.h"

#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QTextCodec>

namespace {

QString normalizedTextureName(const QString& textureName)
{
    QString result = textureName.trimmed().toLower();
    if(result.endsWith(".mmp"))
        result.chop(4);
    return result;
}

QString modelTexturePrefix(const QString& modelName, const QString& textureName)
{
    const QString model = modelName.trimmed().toLower();
    if(model.size() < 4 || !model.startsWith("un") || !textureName.startsWith("un"))
        return QString();
    return model.left(4);
}

void appendUnique(QStringList& values, const QString& value)
{
    if(!value.isEmpty() && !values.contains(value))
        values.append(value);
}

QMutex& resourceFingerprintMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, QString>& resourceFingerprintCache()
{
    static QHash<QString, QString> cache;
    return cache;
}

QString resourcePathSetKey(const QVector<QString>& sources)
{
    QStringList paths;
    paths.reserve(sources.size());
    for(const QString& path : sources)
        paths.append(QFileInfo(path).absoluteFilePath().toLower());
    return paths.join(QLatin1Char('\n'));
}

}

namespace ei {

QString cachedResourceSourceFingerprint(const QVector<QString>& sources)
{
    const QString key = resourcePathSetKey(sources);
    {
        QMutexLocker locker(&resourceFingerprintMutex());
        const auto found = resourceFingerprintCache().constFind(key);
        if(found != resourceFingerprintCache().constEnd())
            return found.value();
    }

    QString fingerprint;
    for(const QString& path : sources)
    {
        const QFileInfo info(path);
        fingerprint += info.absoluteFilePath().toLower() + '|'
            + QString::number(info.size()) + '|'
            + QString::number(info.lastModified().toMSecsSinceEpoch()) + '\n';
    }
    QMutexLocker locker(&resourceFingerprintMutex());
    resourceFingerprintCache().insert(key, fingerprint);
    return fingerprint;
}

void invalidateResourceSourceFingerprintCache()
{
    QMutexLocker locker(&resourceFingerprintMutex());
    resourceFingerprintCache().clear();
}

QVector<QString> orderedResourceSources(const QVector<QString>& explicitSources)
{
    QVector<QString> directoryOrder;
    QMap<QString, QVector<QString>> explicitByDirectory;
    for(const QString& source : explicitSources)
    {
        const QFileInfo info(source);
        const QString directory = info.isDir() ? info.absoluteFilePath()
                                               : info.absolutePath();
        const QString key = QDir::cleanPath(directory).toLower();
        if(!explicitByDirectory.contains(key))
            directoryOrder.append(directory);
        if(info.isFile())
            explicitByDirectory[key].append(info.absoluteFilePath());
    }

    QVector<QString> result;
    QSet<QString> seen;
    const auto appendUnique = [&result, &seen](const QString& path)
    {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        const QString key = QDir::cleanPath(absolute).toLower();
        if(seen.contains(key))
            return;
        seen.insert(key);
        result.append(absolute);
    };

    for(const QString& directory : directoryOrder)
    {
        const QString key = QDir::cleanPath(directory).toLower();
        for(const QString& source : explicitByDirectory.value(key))
            appendUnique(source);

        // Mod resource overrides are first-match. Preserve duplicate-key order
        // from config.ini instead of alphabetizing the directory and silently
        // selecting a lower-priority archive with the same member name.
        const QString configPath = QDir(directory).absoluteFilePath("../config.ini");
        QFile config(configPath);
        if(config.open(QIODevice::ReadOnly))
        {
            QTextCodec* codec = QTextCodec::codecForName("CP1251");
            const QByteArray bytes = config.readAll();
            const QString text = codec ? codec->toUnicode(bytes)
                                       : QString::fromLocal8Bit(bytes);
            bool resources = false;
            for(const QString& rawLine : text.split(QRegularExpression("[\\r\\n]+")))
            {
                const QString line = rawLine.trimmed();
                if(line.startsWith('[') && line.endsWith(']'))
                {
                    resources = line.mid(1, line.size() - 2)
                        .compare("RES", Qt::CaseInsensitive) == 0;
                    continue;
                }
                if(!resources || line.isEmpty() || line.startsWith(';')
                   || line.startsWith('#'))
                    continue;
                const int equals = line.indexOf('=');
                if(equals < 0)
                    continue;
                const QString relative = QDir::fromNativeSeparators(
                    line.mid(equals + 1).trimmed());
                const QString routed = QDir(QFileInfo(configPath).absolutePath())
                    .absoluteFilePath(relative);
                if(routed.endsWith(".res", Qt::CaseInsensitive)
                   && QFileInfo(routed).isFile())
                    appendUnique(routed);
            }
        }

        const QDir resourceDirectory(directory);
        for(const QFileInfo& archive : resourceDirectory.entryInfoList(
                {"*.res"}, QDir::Files, QDir::Name | QDir::IgnoreCase))
            appendUnique(archive.absoluteFilePath());
    }
    return result;
}

float textureUvScale(const QString& textureName)
{
    const QString normalized = textureName.trimmed().toLower();
    if(normalized.endsWith(".x2") || normalized.endsWith(".x2.mmp")
            || normalized.endsWith("_x2") || normalized.endsWith("_x2.mmp"))
        return 2.0f;
    return 1.0f;
}

float textureUvScaleForBounds(const QString& textureName, float minU, float maxU,
                              float minV, float maxV)
{
    const float namedScale = textureUvScale(textureName);
    if(namedScale <= 1.0f)
        return namedScale;

    // Ordinary x2 equipment FIGs store their UVs in one 0.5 x 0.5 atlas
    // quadrant and rely on the renderer's repeat transform. Some integral
    // creature weapons reuse the same equipment MMP after their FIG UVs have
    // already been expanded to the full atlas. Scaling those a second time
    // selects unrelated islands. Determine which representation the asset
    // contains from its measured UV extent rather than its model family.
    constexpr float packedQuadrantLimit = 0.5001f;
    if(maxU - minU > packedQuadrantLimit || maxV - minV > packedQuadrantLimit)
        return 1.0f;
    return namedScale;
}

bool usesUnlayeredHumanoidTexture(const QString& partName)
{
    const QString normalized = partName.trimmed().toLower();
    // Clothing/armor atlases share body UV space, but the face and selected
    // hair remain sourced from the profile skin atlas. Applying the composed
    // clothing atlas to these components can paint pants/armor over a face.
    return normalized == "hd" || normalized.startsWith("hr.");
}

QString textureCacheKey(const QString& textureName, const QString& modelName)
{
    const QString texture = normalizedTextureName(textureName);
    const QString prefix = modelTexturePrefix(modelName, texture);
    return prefix.isEmpty() ? texture : prefix + '|' + texture;
}

QStringList textureResourceCandidates(const QString& textureName, const QString& modelName)
{
    const QString texture = normalizedTextureName(textureName);
    QStringList result;
    if(texture.isEmpty())
        return result;

    appendUnique(result, texture + ".mmp");

    static const QRegularExpression scaleSuffix("_x([0-9]+)$");
    const QRegularExpressionMatch match = scaleSuffix.match(texture);
    if(!match.hasMatch())
        return result;

    QString archiveStem = texture.left(match.capturedStart()) + ".x" + match.captured(1);
    const QString prefix = modelTexturePrefix(modelName, texture);
    if(!prefix.isEmpty())
        appendUnique(result, prefix + archiveStem.mid(2) + ".mmp");
    appendUnique(result, archiveStem + ".mmp");

    // Some shipped maps refer to a numbered skin variation that is not present
    // in any configured archive. The game tolerates these references; for the
    // editor view, prefer the nearest lower member of the same model-qualified
    // family instead of the unrelated diagnostic texture. The stored MOB value
    // is never changed by this lookup fallback.
    static const QRegularExpression numberedVariant("^(.*_)([0-9]+)(_x[0-9]+)$");
    const QRegularExpressionMatch variant = numberedVariant.match(texture);
    if(!prefix.isEmpty() && variant.hasMatch())
    {
        const QString digits = variant.captured(2);
        const int requestedVariation = digits.toInt();
        const int lowestVariation = qMax(0, requestedVariation - 8);
        for(int variation = requestedVariation - 1; variation >= lowestVariation; --variation)
        {
            const QString fallbackTexture = variant.captured(1)
                    + QString("%1").arg(variation, digits.size(), 10, QChar('0'))
                    + variant.captured(3);
            const QRegularExpressionMatch fallbackScale = scaleSuffix.match(fallbackTexture);
            QString fallbackStem = fallbackTexture.left(fallbackScale.capturedStart())
                    + ".x" + fallbackScale.captured(1);
            appendUnique(result, prefix + fallbackStem.mid(2) + ".mmp");
        }
    }
    return result;
}

QStringList terrainTextureResourceNames(const QString& mapName, int textureAtlasCount)
{
    QString normalizedMapName = mapName.trimmed().toLower();
    if(normalizedMapName.endsWith(".mpr"))
        normalizedMapName.chop(4);
    else if(normalizedMapName.endsWith(".mp"))
        normalizedMapName.chop(3);

    QStringList result;
    if(normalizedMapName.isEmpty() || textureAtlasCount <= 0)
        return result;
    for(int atlas = 0; atlas < textureAtlasCount; ++atlas)
        result.append(QString("%1%2.mmp").arg(normalizedMapName).arg(atlas, 3, 10, QChar('0')));
    return result;
}

}
