#include "world_map_layout_document.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace {
const int kSchemaVersion = 1;
const int kLayoutAlgorithmVersion = 1;

QString key(const QString& value)
{
    return value.trimmed().toCaseFolded();
}

QString zoneKey(const QString& profileId, const QString& allodId,
                const QString& zoneId)
{
    const QChar separator(0x1f);
    return key(profileId) + separator + key(allodId)
        + separator + key(zoneId);
}

bool finitePoint(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool safeIdentity(const QString& value)
{
    const QString trimmed = value.trimmed();
    if(trimmed.isEmpty() || trimmed.size() > 256
       || trimmed == QStringLiteral(".") || trimmed == QStringLiteral("..")
       || trimmed.contains(QLatin1Char('/'))
       || trimmed.contains(QLatin1Char('\\'))
       || trimmed.contains(QLatin1Char(':')))
        return false;
    for(const QChar character : trimmed)
        if(character.category() == QChar::Other_Control)
            return false;
    return true;
}

bool validZone(const SWorldMapZoneOrigin& zone, QString* error = nullptr)
{
    if(!safeIdentity(zone.profileId) || !safeIdentity(zone.allodId)
       || !safeIdentity(zone.zoneId) || !safeIdentity(zone.mprIdentity)
       || !safeIdentity(zone.componentAnchorZoneId))
    {
        if(error)
            *error = QStringLiteral(
                "World-map identities must be stable names, not paths.");
        return false;
    }
    if(!finitePoint(zone.origin))
    {
        if(error)
            *error = QStringLiteral("World-map origins must be finite numbers.");
        return false;
    }
    return true;
}

bool zoneLess(const SWorldMapZoneOrigin& left,
              const SWorldMapZoneOrigin& right)
{
    if(key(left.profileId) != key(right.profileId))
        return key(left.profileId) < key(right.profileId);
    if(key(left.allodId) != key(right.allodId))
        return key(left.allodId) < key(right.allodId);
    return key(left.zoneId) < key(right.zoneId);
}

QByteArray serialize(const QVector<SWorldMapZoneOrigin>& input,
                     QString* error)
{
    QVector<SWorldMapZoneOrigin> zones = input;
    std::sort(zones.begin(), zones.end(), zoneLess);
    QSet<QString> identities;
    for(const SWorldMapZoneOrigin& zone : zones)
    {
        if(!validZone(zone, error))
            return QByteArray();
        const QString identity = zoneKey(
            zone.profileId, zone.allodId, zone.zoneId);
        if(identities.contains(identity))
        {
            if(error)
                *error = QStringLiteral(
                    "The world-map document contains a duplicate zone identity.");
            return QByteArray();
        }
        identities.insert(identity);
    }

    QJsonArray profiles;
    int profileStart = 0;
    while(profileStart < zones.size())
    {
        const QString profileKey = key(zones.at(profileStart).profileId);
        int profileEnd = profileStart + 1;
        while(profileEnd < zones.size()
              && key(zones.at(profileEnd).profileId) == profileKey)
            ++profileEnd;

        QJsonObject profileObject;
        profileObject.insert(QStringLiteral("profileId"),
                             zones.at(profileStart).profileId);
        QJsonArray allods;
        int allodStart = profileStart;
        while(allodStart < profileEnd)
        {
            const QString allodKey = key(zones.at(allodStart).allodId);
            int allodEnd = allodStart + 1;
            while(allodEnd < profileEnd
                  && key(zones.at(allodEnd).allodId) == allodKey)
                ++allodEnd;

            QJsonObject allodObject;
            allodObject.insert(QStringLiteral("allodId"),
                               zones.at(allodStart).allodId);
            QJsonArray zoneArray;
            for(int index = allodStart; index < allodEnd; ++index)
            {
                const SWorldMapZoneOrigin& zone = zones.at(index);
                QJsonObject origin;
                origin.insert(QStringLiteral("x"), zone.origin.x());
                origin.insert(QStringLiteral("y"), zone.origin.y());
                QJsonObject zoneObject;
                zoneObject.insert(QStringLiteral("zoneId"), zone.zoneId);
                zoneObject.insert(QStringLiteral("mprIdentity"),
                                  zone.mprIdentity);
                zoneObject.insert(QStringLiteral("origin"), origin);
                zoneObject.insert(QStringLiteral("provenance"),
                    worldMapOriginProvenanceName(zone.provenance));
                zoneObject.insert(QStringLiteral("componentAnchorZoneId"),
                                  zone.componentAnchorZoneId);
                zoneArray.append(zoneObject);
            }
            allodObject.insert(QStringLiteral("zones"), zoneArray);
            allods.append(allodObject);
            allodStart = allodEnd;
        }
        profileObject.insert(QStringLiteral("allods"), allods);
        profiles.append(profileObject);
        profileStart = profileEnd;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    root.insert(QStringLiteral("layoutAlgorithmVersion"),
                kLayoutAlgorithmVersion);
    root.insert(QStringLiteral("coordinateSystem"),
                QStringLiteral("mpr-local-xy"));
    root.insert(QStringLiteral("profiles"), profiles);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool parse(const QByteArray& bytes, QVector<SWorldMapZoneOrigin>* output,
           QString* error)
{
    if(!output)
        return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if(parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if(error)
            *error = QStringLiteral("The JSON is invalid: %1")
                .arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonValue schemaVersion = root.value(
        QStringLiteral("schemaVersion"));
    const QJsonValue layoutAlgorithmVersion = root.value(
        QStringLiteral("layoutAlgorithmVersion"));
    if(!schemaVersion.isDouble()
       || schemaVersion.toDouble() != double(kSchemaVersion))
    {
        if(error)
            *error = QStringLiteral(
                "The schemaVersion is missing or unsupported.");
        return false;
    }
    if(!layoutAlgorithmVersion.isDouble()
       || layoutAlgorithmVersion.toDouble()
            != double(kLayoutAlgorithmVersion)
       || root.value(QStringLiteral("coordinateSystem")).toString()
            != QStringLiteral("mpr-local-xy")
       || !root.value(QStringLiteral("profiles")).isArray())
    {
        if(error)
            *error = QStringLiteral(
                "The coordinate-system metadata is missing or unsupported.");
        return false;
    }

    QVector<SWorldMapZoneOrigin> parsed;
    QSet<QString> identities;
    QSet<QString> profileIdentities;
    const QJsonArray profiles = root.value(QStringLiteral("profiles")).toArray();
    for(const QJsonValue& profileValue : profiles)
    {
        if(!profileValue.isObject())
        {
            if(error) *error = QStringLiteral("A profile entry is not an object.");
            return false;
        }
        const QJsonObject profile = profileValue.toObject();
        const QString profileId = profile.value(
            QStringLiteral("profileId")).toString();
        if(!safeIdentity(profileId)
           || !profile.value(QStringLiteral("allods")).isArray())
        {
            if(error) *error = QStringLiteral("A profile entry is incomplete.");
            return false;
        }
        const QString normalizedProfileId = key(profileId);
        if(profileIdentities.contains(normalizedProfileId))
        {
            if(error)
                *error = QStringLiteral(
                    "The document contains a duplicate profile identity.");
            return false;
        }
        profileIdentities.insert(normalizedProfileId);
        const QJsonArray allods = profile.value(QStringLiteral("allods")).toArray();
        QSet<QString> allodIdentities;
        for(const QJsonValue& allodValue : allods)
        {
            if(!allodValue.isObject())
            {
                if(error) *error = QStringLiteral("An allod entry is not an object.");
                return false;
            }
            const QJsonObject allod = allodValue.toObject();
            const QString allodId = allod.value(
                QStringLiteral("allodId")).toString();
            if(!safeIdentity(allodId)
               || !allod.value(QStringLiteral("zones")).isArray())
            {
                if(error) *error = QStringLiteral("An allod entry is incomplete.");
                return false;
            }
            const QString normalizedAllodId = key(allodId);
            if(allodIdentities.contains(normalizedAllodId))
            {
                if(error)
                    *error = QStringLiteral(
                        "A profile contains a duplicate allod identity.");
                return false;
            }
            allodIdentities.insert(normalizedAllodId);
            const QJsonArray zoneArray = allod.value(
                QStringLiteral("zones")).toArray();
            for(const QJsonValue& zoneValue : zoneArray)
            {
                if(!zoneValue.isObject())
                {
                    if(error) *error = QStringLiteral("A zone entry is not an object.");
                    return false;
                }
                const QJsonObject object = zoneValue.toObject();
                const QJsonValue originValue = object.value(
                    QStringLiteral("origin"));
                if(!originValue.isObject())
                {
                    if(error) *error = QStringLiteral("A zone origin is missing.");
                    return false;
                }
                const QJsonObject origin = originValue.toObject();
                if(!origin.value(QStringLiteral("x")).isDouble()
                   || !origin.value(QStringLiteral("y")).isDouble())
                {
                    if(error) *error = QStringLiteral("A zone origin is not numeric.");
                    return false;
                }
                SWorldMapZoneOrigin zone;
                zone.profileId = profileId;
                zone.allodId = allodId;
                zone.zoneId = object.value(QStringLiteral("zoneId")).toString();
                zone.mprIdentity = object.value(
                    QStringLiteral("mprIdentity")).toString();
                zone.origin = QPointF(origin.value(QStringLiteral("x")).toDouble(),
                                      origin.value(QStringLiteral("y")).toDouble());
                zone.componentAnchorZoneId = object.value(
                    QStringLiteral("componentAnchorZoneId")).toString();
                if(!parseWorldMapOriginProvenance(
                       object.value(QStringLiteral("provenance")).toString(),
                       &zone.provenance)
                   || !validZone(zone, error))
                    return false;
                const QString identity = zoneKey(
                    profileId, allodId, zone.zoneId);
                if(identities.contains(identity))
                {
                    if(error)
                        *error = QStringLiteral(
                            "The document contains a duplicate zone identity.");
                    return false;
                }
                identities.insert(identity);
                parsed.append(zone);
            }
        }
    }
    for(const SWorldMapZoneOrigin& zone : parsed)
    {
        bool anchorExists = false;
        for(const SWorldMapZoneOrigin& candidate : parsed)
            if(key(candidate.profileId) == key(zone.profileId)
               && key(candidate.allodId) == key(zone.allodId)
               && key(candidate.zoneId) == key(zone.componentAnchorZoneId))
            {
                anchorExists = true;
                break;
            }
        if(!anchorExists)
        {
            if(error)
                *error = QStringLiteral(
                    "A zone refers to a missing component anchor identity.");
            return false;
        }
    }
    *output = parsed;
    return true;
}
}

CWorldMapLayoutDocument::CWorldMapLayoutDocument(const QString& path)
    : m_path(path.isEmpty() ? defaultPath() : QDir::cleanPath(path))
{
}

QString CWorldMapLayoutDocument::defaultPath()
{
    return QDir(QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation)).filePath(
            QStringLiteral("EI Mapper Endurance/world-map-layouts.json"));
}

void CWorldMapLayoutDocument::setProfileDirectory(
    const QString& root, const QString& profileId, const QString& legacyPath)
{
    const QString destination = root.trimmed().isEmpty() || !QDir::isAbsolutePath(root)
        || !safeIdentity(profileId) ? QString()
        : QDir::cleanPath(QDir(root).filePath(QStringLiteral("world-map-layouts.json")));
    if(m_path == destination && m_storageProfileId == profileId
       && m_legacyPath == legacyPath)
        return;
    m_path = destination;
    m_storageProfileId = profileId;
    m_legacyPath = legacyPath;
    m_loadState = ELoadState::NotLoaded;
    m_hasPersistentFile = false;
    m_loadWarning.clear();
    m_zones.clear();
}

bool CWorldMapLayoutDocument::ensureLoaded()
{
    if(m_loadState != ELoadState::NotLoaded)
        return m_loadState == ELoadState::Ready;
    if(m_path.isEmpty())
    {
        m_loadState = ELoadState::Blocked;
        m_loadWarning = QStringLiteral("The active profile has no world-map storage directory.");
        return false;
    }
    const QFileInfo info(m_path);
    if(!info.exists())
    {
        if(!m_legacyPath.isEmpty() && QFileInfo::exists(m_legacyPath))
        {
            CWorldMapLayoutDocument legacy(m_legacyPath);
            if(!legacy.ensureLoaded())
            {
                m_loadState = ELoadState::Blocked;
                m_loadWarning = legacy.m_loadWarning;
                return false;
            }
            // Leave the legacy file intact as a recovery copy. Do not carry
            // other profiles into a newly created profile-local document.
            m_zones = profileZones(legacy.m_zones, m_storageProfileId);
        }
        m_loadState = ELoadState::Ready;
        return true;
    }
    QFile file(m_path);
    if(!file.open(QIODevice::ReadOnly))
    {
        m_loadState = ELoadState::Blocked;
        m_loadWarning = QStringLiteral(
            "The world-map layout document could not be read and was left unchanged: %1")
            .arg(file.errorString());
        return false;
    }
    QString parseError;
    QVector<SWorldMapZoneOrigin> parsed;
    if(!parse(file.readAll(), &parsed, &parseError))
    {
        m_loadState = ELoadState::Blocked;
        m_loadWarning = QStringLiteral(
            "The world-map layout document is damaged or unsupported and was left unchanged. Inferred positions are being used temporarily. %1")
            .arg(parseError);
        return false;
    }
    m_zones = parsed;
    m_hasPersistentFile = true;
    m_loadState = ELoadState::Ready;
    return true;
}

bool CWorldMapLayoutDocument::save(
    const QVector<SWorldMapZoneOrigin>& zones, QString* error) const
{
    if(m_path.isEmpty() || (!m_hasPersistentFile && QFileInfo::exists(m_path)))
    {
        if(error) *error = QStringLiteral(
            "World-map storage is unavailable or a destination appeared after loading. Reload the profile; the existing file was left unchanged.");
        return false;
    }
    QString serializationError;
    const QByteArray bytes = serialize(zones, &serializationError);
    if(bytes.isEmpty())
    {
        if(error) *error = serializationError;
        return false;
    }
    if(!QDir().mkpath(QFileInfo(m_path).absolutePath()))
    {
        if(error)
            *error = QStringLiteral(
                "Could not create the world-map layout directory.");
        return false;
    }
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly))
    {
        if(error) *error = file.errorString();
        return false;
    }
    if(file.write(bytes) != bytes.size())
    {
        if(error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if(!file.commit())
    {
        if(error) *error = file.errorString();
        return false;
    }
    if(error) error->clear();
    return true;
}

QVector<SWorldMapZoneOrigin> CWorldMapLayoutDocument::profileZones(
    const QVector<SWorldMapZoneOrigin>& zones,
    const QString& profileId) const
{
    QVector<SWorldMapZoneOrigin> result;
    for(const SWorldMapZoneOrigin& zone : zones)
        if(key(zone.profileId) == key(profileId))
            result.append(zone);
    std::sort(result.begin(), result.end(), zoneLess);
    return result;
}

SWorldMapZoneOrigin CWorldMapLayoutDocument::alignInferredZone(
    const SWorldMapZoneOrigin& inferred,
    const QVector<SWorldMapZoneOrigin>& inference,
    const QVector<SWorldMapZoneOrigin>& existing) const
{
    const SWorldMapZoneOrigin* alignment = nullptr;
    const SWorldMapZoneOrigin* alignmentInference = nullptr;
    for(const SWorldMapZoneOrigin& candidate : existing)
    {
        if(key(candidate.profileId) != key(inferred.profileId)
           || key(candidate.allodId) != key(inferred.allodId))
            continue;
        const SWorldMapZoneOrigin* candidateInference = CWorldMapLayout::zone(
            inference, inferred.profileId, inferred.allodId, candidate.zoneId);
        if(!candidateInference
           || key(candidateInference->componentAnchorZoneId)
                != key(inferred.componentAnchorZoneId)
           || key(candidate.mprIdentity)
                != key(candidateInference->mprIdentity))
            continue;
        if(!alignment || key(candidate.zoneId) < key(alignment->zoneId))
        {
            alignment = &candidate;
            alignmentInference = candidateInference;
        }
    }

    SWorldMapZoneOrigin result = inferred;
    if(alignment && alignmentInference)
    {
        result.origin += alignment->origin - alignmentInference->origin;
        result.componentAnchorZoneId = alignment->componentAnchorZoneId;
    }
    return result;
}

SWorldMapLayoutUpdateResult CWorldMapLayoutDocument::loadAndUpdate(
    const QString& profileId,
    const QVector<SMapCatalogEntry>& topology)
{
    SWorldMapLayoutUpdateResult result;
    const SWorldMapInferenceResult inference = CWorldMapLayout::infer(
        profileId, topology);
    result.diagnostics = inference.diagnostics;
    if(!ensureLoaded())
    {
        result.zones = inference.zones;
        result.warning = m_loadWarning;
        result.storageBlocked = true;
        return result;
    }
    if(!safeIdentity(profileId))
    {
        result.zones = inference.zones;
        result.warning = QStringLiteral(
            "The active resource profile has no safe stable identity. Inferred world-map positions are temporary and were not saved.");
        return result;
    }

    QVector<SWorldMapZoneOrigin> candidate = m_zones;
    bool changed = !m_hasPersistentFile && !candidate.isEmpty();
    for(const SWorldMapZoneOrigin& inferred : inference.zones)
    {
        QString validationError;
        if(!validZone(inferred, &validationError))
        {
            result.diagnostics.append(validationError);
            continue;
        }
        const SWorldMapZoneOrigin* stored = CWorldMapLayout::zone(
            candidate, inferred.profileId, inferred.allodId, inferred.zoneId);
        if(stored)
        {
            if(key(stored->mprIdentity) != key(inferred.mprIdentity))
                result.diagnostics.append(QStringLiteral(
                    "Stored MPR identity differs from map.txt profile:%1 allod:%2 zone:%3; saved coordinates were preserved.")
                    .arg(inferred.profileId, inferred.allodId, inferred.zoneId));
            continue;
        }
        candidate.append(alignInferredZone(inferred, inference.zones,
                                           candidate));
        changed = true;
    }

    if(changed)
    {
        QString saveError;
        if(!save(candidate, &saveError))
        {
            result.zones = profileZones(candidate, profileId);
            result.warning = QStringLiteral(
                "World-map coordinates could not be saved atomically. The existing document was left unchanged and inferred additions are temporary. %1")
                .arg(saveError);
            return result;
        }
        m_zones = candidate;
        m_hasPersistentFile = true;
    }
    result.zones = profileZones(m_zones, profileId);
    result.persisted = m_hasPersistentFile;
    return result;
}

bool CWorldMapLayoutDocument::setZoneOrigin(
    const QString& profileId, const QString& allodId,
    const QString& zoneId, const QPointF& origin, QString* error)
{
    if(!ensureLoaded())
    {
        if(error) *error = m_loadWarning;
        return false;
    }
    if(!finitePoint(origin))
    {
        if(error) *error = QStringLiteral("World-map origins must be finite.");
        return false;
    }
    QVector<SWorldMapZoneOrigin> candidate = m_zones;
    bool found = false;
    for(SWorldMapZoneOrigin& zone : candidate)
        if(zoneKey(zone.profileId, zone.allodId, zone.zoneId)
           == zoneKey(profileId, allodId, zoneId))
        {
            zone.origin = origin;
            zone.provenance = EWorldMapOriginProvenance::Manual;
            found = true;
            break;
        }
    if(!found)
    {
        if(error) *error = QStringLiteral("The requested world-map zone is unknown.");
        return false;
    }
    if(!save(candidate, error))
        return false;
    m_zones = candidate;
    m_hasPersistentFile = true;
    return true;
}

bool CWorldMapLayoutDocument::resetZoneToInferred(
    const QString& profileId, const QString& allodId,
    const QString& zoneId,
    const QVector<SMapCatalogEntry>& topology, QString* error)
{
    if(!ensureLoaded())
    {
        if(error) *error = m_loadWarning;
        return false;
    }
    const SWorldMapInferenceResult inference = CWorldMapLayout::infer(
        profileId, topology);
    const SWorldMapZoneOrigin* inferred = CWorldMapLayout::zone(
        inference.zones, profileId, allodId, zoneId);
    if(!inferred)
    {
        if(error)
            *error = QStringLiteral(
                "The requested zone has no inferred map.txt position.");
        return false;
    }

    QVector<SWorldMapZoneOrigin> candidate;
    bool found = false;
    for(const SWorldMapZoneOrigin& zone : m_zones)
    {
        if(zoneKey(zone.profileId, zone.allodId, zone.zoneId)
           == zoneKey(profileId, allodId, zoneId))
        {
            found = true;
            continue;
        }
        candidate.append(zone);
    }
    if(!found)
    {
        if(error) *error = QStringLiteral("The requested world-map zone is unknown.");
        return false;
    }
    candidate.append(alignInferredZone(*inferred, inference.zones, candidate));
    if(!save(candidate, error))
        return false;
    m_zones = candidate;
    m_hasPersistentFile = true;
    return true;
}

bool CWorldMapLayoutDocument::resetAllodLayout(
    const QString& profileId, const QString& allodId,
    const QVector<SMapCatalogEntry>& topology, QString* error)
{
    if(!ensureLoaded())
    {
        if(error) *error = m_loadWarning;
        return false;
    }
    const SWorldMapInferenceResult inference = CWorldMapLayout::infer(
        profileId, topology);
    QVector<SWorldMapZoneOrigin> candidate;
    for(const SWorldMapZoneOrigin& zone : m_zones)
        if(key(zone.profileId) != key(profileId)
           || key(zone.allodId) != key(allodId))
            candidate.append(zone);
    int added = 0;
    for(const SWorldMapZoneOrigin& zone : inference.zones)
        if(key(zone.allodId) == key(allodId))
        {
            candidate.append(zone);
            ++added;
        }
    if(added == 0)
    {
        if(error)
            *error = QStringLiteral(
                "The requested allod has no inferred map.txt layout.");
        return false;
    }
    if(!save(candidate, error))
        return false;
    m_zones = candidate;
    m_hasPersistentFile = true;
    return true;
}
