#include "resource_name.h"

#include <QRegularExpression>

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

}

namespace ei {

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
