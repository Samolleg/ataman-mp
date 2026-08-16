#include "terrain_paint_diagnostic.h"

#include <QJsonArray>
#include <QSet>

namespace {
QJsonObject signatureJson(const STerrainTransitionSignature& signature)
{
    QJsonObject result;
    result.insert(QStringLiteral("x0y0"), signature.northWest);
    result.insert(QStringLiteral("x1y0"), signature.northEast);
    result.insert(QStringLiteral("x1y1"), signature.southEast);
    result.insert(QStringLiteral("x0y1"), signature.southWest);
    return result;
}

QString originName(ETerrainWorkingTileOrigin origin)
{
    switch(origin)
    {
    case ETerrainWorkingTileOrigin::AuthoredMap:
        return QStringLiteral("authored-current-map");
    case ETerrainWorkingTileOrigin::ImportedAuthored:
        return QStringLiteral("authored-global");
    case ETerrainWorkingTileOrigin::CanonicalGenerated:
        return QStringLiteral("canonical-generated");
    default:
        return QStringLiteral("unknown");
    }
}
}

QJsonObject terrainWorkingPhysicalIdentityJson(
    const STerrainWorkingTileIdentity& working,
    const QString& currentMapNamespace,
    const QVector<STerrainGlobalMaterialFamily>& materialFamilies,
    const QMap<qint32, STerrainGlobalMaterialSource>& materialSources,
    const QMap<qint32, QByteArray>& materialPixelSha256)
{
    QJsonObject identity;
    identity.insert(QStringLiteral("origin"), originName(working.origin));
    identity.insert(QStringLiteral("workingSlot"), working.workingSlot);
    const QString sourceNamespace =
        working.origin == ETerrainWorkingTileOrigin::AuthoredMap
            ? currentMapNamespace : working.sourceNamespace;
    identity.insert(QStringLiteral("sourceNamespace"), sourceNamespace);
    identity.insert(QStringLiteral("sourceSlot"), working.sourceSlot);
    if(working.sourceSlot >= 0 && !working.physicalPixelSha256.isEmpty())
        identity.insert(QStringLiteral("sourcePixelSha256"),
                        QString::fromLatin1(working.physicalPixelSha256));
    QJsonArray aliases;
    for(int sourceSlot : working.sourceSlotAliases)
    {
        QJsonObject alias;
        alias.insert(QStringLiteral("sourceNamespace"), sourceNamespace);
        alias.insert(QStringLiteral("sourceSlot"), sourceSlot);
        alias.insert(QStringLiteral("sourcePixelSha256"),
                     QString::fromLatin1(working.physicalPixelSha256));
        aliases.append(alias);
    }
    if(!aliases.isEmpty())
        identity.insert(QStringLiteral("sourceAliases"), aliases);
    identity.insert(QStringLiteral("blendProvenance"), working.blendProvenance);
    identity.insert(QStringLiteral("stockDerivedBlend"),
                    working.stockDerivedBlend);
    identity.insert(QStringLiteral("canonicalGeneratedBlend"),
                    working.canonicalGeneratedBlend);
    identity.insert(QStringLiteral("runtimeTerrainType"),
                    working.runtimeTerrainType);
    identity.insert(QStringLiteral("canonicalGridCorners"),
                    signatureJson(working.canonicalSignature));

    if(working.origin == ETerrainWorkingTileOrigin::CanonicalGenerated)
    {
        QSet<qint32> addedMaterials;
        QJsonArray inputs;
        for(qint32 materialId : {working.canonicalSignature.northWest,
                                 working.canonicalSignature.northEast,
                                 working.canonicalSignature.southEast,
                                 working.canonicalSignature.southWest})
        {
            if(addedMaterials.contains(materialId))
                continue;
            addedMaterials.insert(materialId);
            const auto source = materialSources.constFind(materialId);
            if(source == materialSources.constEnd())
                continue;
            QString materialName;
            for(const STerrainGlobalMaterialFamily& family : materialFamilies)
                if(family.id == materialId)
                {
                    materialName = family.name;
                    break;
                }
            QJsonObject input;
            input.insert(QStringLiteral("materialId"), materialId);
            input.insert(QStringLiteral("material"), materialName);
            input.insert(QStringLiteral("sourceNamespace"),
                         source->mprNamespace);
            input.insert(QStringLiteral("sourceSlot"), source->uniformSlot);
            input.insert(QStringLiteral("atlasIdentity"),
                         QString::fromLatin1(source->atlasIdentity.toHex()));
            const auto pixels = materialPixelSha256.constFind(materialId);
            if(pixels != materialPixelSha256.constEnd())
                input.insert(QStringLiteral("physicalPixelSha256"),
                             QString::fromLatin1(pixels.value()));
            inputs.append(input);
        }
        identity.insert(QStringLiteral("inputUniformSources"), inputs);
    }
    return identity;
}

QJsonObject terrainAuthoredPhysicalIdentityJson(
    int currentMapSlot, const QByteArray& physicalPixelSha256,
    const QString& currentMapNamespace, qint32 runtimeTerrainType,
    const STerrainAtlasProfileSlot* profileSlot)
{
    QJsonObject identity;
    identity.insert(QStringLiteral("origin"),
                    QStringLiteral("authored-current-map"));
    identity.insert(QStringLiteral("workingSlot"), currentMapSlot);
    QString sourceNamespace = currentMapNamespace;
    int sourceSlot = currentMapSlot;
    QByteArray sourcePixelSha256 = physicalPixelSha256;
    if(profileSlot && !profileSlot->sourceSlotAliases.isEmpty())
    {
        if(!profileSlot->sourceNamespace.isEmpty())
            sourceNamespace = profileSlot->sourceNamespace;
        sourceSlot = profileSlot->sourceSlotAliases.front();
        if(!profileSlot->sourcePixelSha256.isEmpty())
            sourcePixelSha256 = profileSlot->sourcePixelSha256;
    }
    identity.insert(QStringLiteral("sourceNamespace"), sourceNamespace);
    identity.insert(QStringLiteral("sourceSlot"), sourceSlot);
    identity.insert(QStringLiteral("sourcePixelSha256"),
                    QString::fromLatin1(sourcePixelSha256));
    QJsonArray aliases;
    if(profileSlot)
        for(int aliasSlot : profileSlot->sourceSlotAliases)
        {
            QJsonObject alias;
            alias.insert(QStringLiteral("sourceNamespace"), sourceNamespace);
            alias.insert(QStringLiteral("sourceSlot"), aliasSlot);
            alias.insert(QStringLiteral("sourcePixelSha256"),
                         QString::fromLatin1(sourcePixelSha256));
            aliases.append(alias);
        }
    if(!aliases.isEmpty())
        identity.insert(QStringLiteral("sourceAliases"), aliases);
    identity.insert(QStringLiteral("blendProvenance"),
                    QStringLiteral("original map atlas"));
    identity.insert(QStringLiteral("stockDerivedBlend"), false);
    identity.insert(QStringLiteral("canonicalGeneratedBlend"), false);
    identity.insert(QStringLiteral("runtimeTerrainType"), runtimeTerrainType);
    return identity;
}
