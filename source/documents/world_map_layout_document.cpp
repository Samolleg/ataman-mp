#include "world_map_layout_document.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QDirIterator>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace {
const int kSchemaVersion = 2;
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

// Merge known fields by stable identities, retaining extension properties and
// empty world/profile containers. Removed zone identities are never resurrected.
QJsonArray preserveGroupProperties(const QJsonArray& previous, const QJsonArray& current, int level)
{
    const QString identity = level == 0 ? "profileId" : level == 1 ? "allodId" : "zoneId";
    const QString children = level == 0 ? "allods" : "zones";
    QJsonArray merged;
    QSet<QString> present;
    for(const auto& value : current)
    {
        const QJsonObject fresh = value.toObject();
        const QString id = key(fresh.value(identity).toString());
        present.insert(id);
        QJsonObject old;
        for(const auto& prior : previous)
            if(key(prior.toObject().value(identity).toString()) == id) {old=prior.toObject();break;}
        QJsonObject result = old;
        for(auto field=fresh.constBegin();field!=fresh.constEnd();++field) result.insert(field.key(),field.value());
        if(level < 2)
            result.insert(children,preserveGroupProperties(old.value(children).toArray(),fresh.value(children).toArray(),level+1));
        else
        {
            QJsonObject origin=old.value("origin").toObject();
            const auto coordinates=fresh.value("origin").toObject();
            for(auto field=coordinates.constBegin();field!=coordinates.constEnd();++field) origin.insert(field.key(),field.value());
            result.insert("origin",origin);
        }
        merged.append(result);
    }
    if(level < 2)
        for(const auto& prior : previous)
        {
            auto group=prior.toObject();
            if(present.contains(key(group.value(identity).toString()))) continue;
            group.insert(children,preserveGroupProperties(group.value(children).toArray(),{},level+1));
            merged.append(group);
        }
    return merged;
}

QByteArray preserveDocumentProperties(const QByteArray& previous, const QByteArray& current)
{
    QJsonObject result=QJsonDocument::fromJson(previous).object();
    const auto fresh=QJsonDocument::fromJson(current).object();
    const auto profiles=preserveGroupProperties(result.value("profiles").toArray(),fresh.value("profiles").toArray(),0);
    for(auto field=fresh.constBegin();field!=fresh.constEnd();++field) result.insert(field.key(),field.value());
    result.insert("profiles",profiles);
    return QJsonDocument(result).toJson(QJsonDocument::Indented);
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
                zoneObject.insert(QStringLiteral("name"), zone.displayName);
                zoneObject.insert(QStringLiteral("mprPath"), zone.mprPath);
                zoneObject.insert(QStringLiteral("mobPath"), zone.mobPath);
                zoneObject.insert(QStringLiteral("sectorWidth"), zone.sectorWidth);
                zoneObject.insert(QStringLiteral("sectorHeight"), zone.sectorHeight);
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
       || (schemaVersion.toDouble() != 1.0
           && schemaVersion.toDouble() != double(kSchemaVersion)))
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
                zone.displayName = object.value(QStringLiteral("name")).toString();
                zone.mprPath = object.value(QStringLiteral("mprPath")).toString();
                zone.mobPath = object.value(QStringLiteral("mobPath")).toString();
                zone.sectorWidth = object.value(QStringLiteral("sectorWidth")).toInt();
                zone.sectorHeight = object.value(QStringLiteral("sectorHeight")).toInt();
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
    m_diskBytes.clear();
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
    m_diskBytes = file.readAll();
    if(!parse(m_diskBytes, &parsed, &parseError))
    {
        m_loadState = ELoadState::Blocked;
        m_loadWarning = QStringLiteral(
            "The world-map layout document is damaged or unsupported and was left unchanged. No editor-world maps were loaded. %1")
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
    QByteArray bytes = serialize(zones, &serializationError);
    if(bytes.isEmpty())
    {
        if(error) *error = serializationError;
        return false;
    }
    bytes = preserveDocumentProperties(m_diskBytes, bytes);
    if(!QDir().mkpath(QFileInfo(m_path).absolutePath()))
    {
        if(error)
            *error = QStringLiteral(
                "Could not create the world-map layout directory.");
        return false;
    }
    QLockFile lock(m_path + QStringLiteral(".lock"));
    if(!lock.tryLock(0))
    {
        if(error) *error = QStringLiteral("Another editor is updating this world. Retry after it finishes.");
        return false;
    }
    if(m_hasPersistentFile)
    {
        QFile current(m_path);
        if(!current.open(QIODevice::ReadOnly) || current.readAll() != m_diskBytes)
        {
            if(error) *error = QStringLiteral("The world file changed outside this editor. Reload the profile before saving.");
            return false;
        }
        if(QJsonDocument::fromJson(m_diskBytes).object().value("schemaVersion").toInt() == 1)
        {
            const QString backup = m_path + QStringLiteral(".before-v2.bak");
            if(QFileInfo::exists(backup))
            {
                QFile prior(backup);
                if(!prior.open(QIODevice::ReadOnly) || prior.readAll() != m_diskBytes)
                {
                    if(error) *error = QStringLiteral("A different migration backup already exists; it was preserved.");
                    return false;
                }
            }
            else if(!QFile::copy(m_path, backup))
            {
                if(error) *error = QStringLiteral("Could not back up the old world layout before migration.");
                return false;
            }
        }
    }
    else if(QFileInfo::exists(m_path))
    {
        if(error) *error = QStringLiteral("A world file appeared after loading. Reload before saving.");
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
    m_diskBytes = bytes;
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


QString CWorldMapLayoutDocument::absoluteResourcePath(const QString& path) const
{
    if(path.isEmpty()) return QString();
    return QDir::cleanPath(QDir(QFileInfo(m_path).absolutePath()).absoluteFilePath(path));
}

QString CWorldMapLayoutDocument::portableResourcePath(const QString& path) const
{
    if(path.isEmpty()) return QString();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const QString relative = QDir(QFileInfo(m_path).absolutePath()).relativeFilePath(absolute);
    return relative.startsWith("../") || QDir::isAbsolutePath(relative) ? absolute : relative;
}

SWorldMapLayoutUpdateResult CWorldMapLayoutDocument::loadEditorWorld(
    const QString& profileId, const QStringList& mapRoots)
{
    SWorldMapLayoutUpdateResult result;
    if(!ensureLoaded())
    {
        result.warning = m_loadWarning;
        result.storageBlocked = true;
        return result;
    }
    // Legacy JSON stores MPR identities, never consult map.txt to resolve them.
    QMap<QString, QStringList> files;
    bool needsResolution = false;
    for(const auto& zone : m_zones)
        needsResolution |= key(zone.profileId) == key(profileId) && zone.mprPath.isEmpty();
    if(needsResolution)
    {
        QStringList roots = mapRoots;
        roots.append(QDir(QFileInfo(m_path).absolutePath()).filePath("Maps"));
        QSet<QString> seen;
        for(const QString& root : roots)
        {
            QMap<QString, QStringList> rootFiles;
            QMap<QString, QStringList> directFiles;
            QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
            while(it.hasNext())
            {
                const QFileInfo info(it.next());
                const QString suffix = info.suffix().toLower();
                if(suffix != "mpr" && suffix != "mob") continue;
                const QString path = info.canonicalFilePath();
                if(path.isEmpty() || seen.contains(key(path))) continue;
                seen.insert(key(path));
                rootFiles[key(info.fileName())].append(path);
                if(QDir(info.absolutePath()) == QDir(root))
                    directFiles[key(info.fileName())].append(path);
            }
            // Profile roots are ordered overrides (mod before base game), just
            // like normal map opening. A base copy is not an ambiguity. Prefer
            // direct children over backups/subfolders in the same root; retain
            // ambiguity only among candidates of equal priority.
            for(auto entry = rootFiles.constBegin(); entry != rootFiles.constEnd(); ++entry)
                if(!files.contains(entry.key()))
                    files.insert(entry.key(), directFiles.contains(entry.key())
                                 ? directFiles.value(entry.key()) : entry.value());
        }
    }
    for(auto& zone : m_zones)
    {
        if(key(zone.profileId) != key(profileId)) continue;
        if(zone.mprPath.isEmpty())
        {
            const auto mprs = files.value(key(zone.mprIdentity + ".mpr"));
            if(mprs.size() == 1)
            {
                zone.mprPath = portableResourcePath(mprs.first());
                // Resolve the same-name MOB with the same profile-root priority.
                // Different names can be attached explicitly in the world manager.
                const auto mobs = files.value(key(zone.mprIdentity + ".mob"));
                if(mobs.size() == 1) zone.mobPath = portableResourcePath(mobs.first());
            }
            else result.diagnostics.append(QStringLiteral("Locate MPR for %1: missing or ambiguous file.").arg(zone.mprIdentity));
        }
    }
    result.zones = profileZones(m_zones, profileId);
    result.persisted = m_hasPersistentFile;
    return result;
}

bool CWorldMapLayoutDocument::updateResourceMetadata(const SWorldMapZoneOrigin& verifiedMap)
{
    if(!ensureLoaded() || verifiedMap.sectorWidth <= 0 || verifiedMap.sectorHeight <= 0)
        return false;
    const QString verifiedPath = key(QFileInfo(absoluteResourcePath(verifiedMap.mprPath)).canonicalFilePath());
    if(verifiedPath.isEmpty()) return false;
    for(auto& zone : m_zones)
        if(zoneKey(zone.profileId,zone.allodId,zone.zoneId)
            == zoneKey(verifiedMap.profileId,verifiedMap.allodId,verifiedMap.zoneId))
        {
            if(key(QFileInfo(absoluteResourcePath(zone.mprPath)).canonicalFilePath()) != verifiedPath)
                return false;
            zone.sectorWidth = verifiedMap.sectorWidth;
            zone.sectorHeight = verifiedMap.sectorHeight;
            return true;
        }
    return false;
}

bool CWorldMapLayoutDocument::addEditorMaps(
    const QVector<SWorldMapZoneOrigin>& maps, QString* error)
{
    if(!ensureLoaded()) { if(error) *error = m_loadWarning; return false; }
    QVector<SWorldMapZoneOrigin> candidate = m_zones;
    for(auto map : maps)
    {
        if(!validZone(map, error) || map.mprPath.isEmpty()
           || map.sectorWidth <= 0 || map.sectorHeight <= 0)
        {
            if(error && error->isEmpty()) *error = QStringLiteral("A map requires a valid MPR and positive dimensions.");
            return false;
        }
        const QString full = absoluteResourcePath(map.mprPath);
        if(!QFileInfo(full).isFile() || (!map.mobPath.isEmpty()
            && !QFileInfo(absoluteResourcePath(map.mobPath)).isFile()))
        {
            if(error) *error = QStringLiteral("A selected map file is missing.");
            return false;
        }
        const QString physical = QFileInfo(full).canonicalFilePath();
        for(const auto& existing : candidate)
        {
            if(key(existing.profileId) != key(map.profileId)) continue;
            if(key(existing.zoneId) == key(map.zoneId)
               || (!existing.mprPath.isEmpty() && key(QFileInfo(
                   absoluteResourcePath(existing.mprPath)).canonicalFilePath()) == key(physical)))
            {
                if(error) *error = QStringLiteral("This map is already in the profile world layout.");
                return false;
            }
        }
        map.mprPath = portableResourcePath(full);
        map.mobPath = portableResourcePath(absoluteResourcePath(map.mobPath));
        candidate.append(map);
    }
    // Validate anchor references before committing even for a newly created world.
    QVector<SWorldMapZoneOrigin> checked;
    const QByteArray bytes = serialize(candidate, error);
    if(bytes.isEmpty() || !parse(bytes, &checked, error) || !save(candidate, error)) return false;
    m_zones = candidate; m_hasPersistentFile = true;
    return true;
}

bool CWorldMapLayoutDocument::removeEditorMap(const QString& profileId,
    const QString& worldId, const QString& mapId, QString* error)
{
    if(!ensureLoaded()) { if(error) *error = m_loadWarning; return false; }
    auto candidate = m_zones;
    int removed = 0;
    for(int i = candidate.size()-1; i >= 0; --i)
        if(key(candidate[i].profileId) == key(profileId)
           && key(candidate[i].allodId) == key(worldId) && key(candidate[i].zoneId) == key(mapId))
        { candidate.remove(i); ++removed; }
    if(!removed) { if(error) *error = QStringLiteral("The map is no longer in this world."); return false; }
    QString replacement;
    for(const auto& zone : candidate)
        if(key(zone.profileId) == key(profileId) && key(zone.allodId) == key(worldId))
        { replacement = zone.zoneId; break; }
    for(auto& zone : candidate)
        if(key(zone.profileId) == key(profileId) && key(zone.allodId) == key(worldId)
           && key(zone.componentAnchorZoneId) == key(mapId)) zone.componentAnchorZoneId = replacement;
    if(!save(candidate, error)) return false;
    m_zones = candidate; m_hasPersistentFile = true;
    return true;
}


bool CWorldMapLayoutDocument::replaceEditorMap(const SWorldMapZoneOrigin& map, QString* error)
{
    if(!ensureLoaded()) {if(error)*error=m_loadWarning;return false;}
    if(!validZone(map,error) || map.mprPath.isEmpty() || map.sectorWidth<=0 || map.sectorHeight<=0) return false;
    const QString full=absoluteResourcePath(map.mprPath);
    if(!QFileInfo(full).isFile() || (!map.mobPath.isEmpty() && !QFileInfo(absoluteResourcePath(map.mobPath)).isFile()))
    {if(error)*error=QStringLiteral("A selected map file is missing.");return false;}
    auto candidate=m_zones;int index=-1;
    bool resourceChanged=true;
    for(const auto& old:candidate)
        if(old.profileId==map.profileId && old.allodId==map.allodId && old.zoneId==map.zoneId)
            resourceChanged=key(absoluteResourcePath(old.mprPath))!=key(full);
    for(int i=0;i<candidate.size();++i)
    {
        const auto& existing=candidate[i];
        if(key(existing.profileId)!=key(map.profileId))continue;
        if(existing.zoneId==map.zoneId && existing.allodId==map.allodId) {index=i;continue;}
        if(resourceChanged && !existing.mprPath.isEmpty() && key(QFileInfo(absoluteResourcePath(existing.mprPath)).canonicalFilePath())
            ==key(QFileInfo(full).canonicalFilePath()))
        {if(error)*error=QStringLiteral("This MPR already belongs to another map in the layout.");return false;}
    }
    if(index<0){if(error)*error=QStringLiteral("The map is no longer in the world layout.");return false;}
    candidate[index]=map;
    candidate[index].mprPath=portableResourcePath(full);
    candidate[index].mobPath=portableResourcePath(absoluteResourcePath(map.mobPath));
    if(!save(candidate,error))return false;
    m_zones=candidate;return true;
}
QStringList CWorldMapLayoutDocument::availableProfiles()
{
    if(!ensureLoaded()) return {};
    QStringList result;
    for(const auto& zone : m_zones)
        if(!result.contains(zone.profileId)) result.append(zone.profileId);
    return result;
}
bool CWorldMapLayoutDocument::importProfile(const QString& sourceProfile,
    const QString& targetProfile, QString* error)
{
    const auto fail=[&](const QString& message) { if(error) *error=message; return false; };
    if(!ensureLoaded()) return fail(m_loadWarning);
    if(!m_hasPersistentFile || !safeIdentity(targetProfile)
       || key(sourceProfile)==key(targetProfile))
        return fail(QStringLiteral("Invalid world profile import."));
    if(!m_storageProfileId.isEmpty() && key(targetProfile)!=key(m_storageProfileId))
        return fail(QStringLiteral("Select the destination profile before importing."));
    auto root=QJsonDocument::fromJson(m_diskBytes).object();
    auto profiles=root.value("profiles").toArray();
    QJsonObject source;
    for(const auto& value:profiles)
    {
        const auto profile=value.toObject();
        const auto id=key(profile.value("profileId").toString());
        if(id==key(targetProfile)) return fail(QStringLiteral("The destination world already exists; it was preserved."));
        if(id==key(sourceProfile)) source=profile;
    }
    if(source.isEmpty()) return fail(QStringLiteral("The source world profile was not found."));
    source.insert("profileId",targetProfile);
    profiles.append(source); root.insert("profiles",profiles);
    const auto bytes=QJsonDocument(root).toJson();
    QVector<SWorldMapZoneOrigin> checked;
    if(!parse(bytes,&checked,error)) return false;
    QLockFile lock(m_path+".lock");
    if(!lock.tryLock(0)) return fail(QStringLiteral("Another editor is updating this world."));
    QFile current(m_path);
    if(!current.open(QIODevice::ReadOnly) || current.readAll()!=m_diskBytes)
        return fail(QStringLiteral("The world file changed. Reload before importing."));
    current.close();
    const QString backup=m_path+".before-profile-import.bak";
    if(QFileInfo::exists(backup))
    {
        QFile prior(backup);
        if(!prior.open(QIODevice::ReadOnly) || prior.readAll()!=m_diskBytes)
            return fail(QStringLiteral("A different world import backup exists; it was preserved."));
    }
    else
    {
        QSaveFile prior(backup); prior.setDirectWriteFallback(false);
        if(!prior.open(QIODevice::WriteOnly) || prior.write(m_diskBytes)!=m_diskBytes.size() || !prior.commit())
            return fail(QStringLiteral("Could not back up the world before importing."));
    }
    QSaveFile file(m_path); file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
        return fail(file.errorString());
    m_diskBytes=bytes; m_zones=checked;
    if(error) error->clear();
    return true;
}

bool CWorldMapLayoutDocument::applyChanges(const QVector<SWorldMapLayoutChange>& changes,
                                         bool forward, QString* error)
{
    if(error) error->clear();
    if(!ensureLoaded()) { if(error) *error=m_loadWarning; return false; }
    const auto same = [](const SWorldMapZoneOrigin& a, const SWorldMapZoneOrigin& b) {
        return a.profileId==b.profileId && a.allodId==b.allodId && a.zoneId==b.zoneId
            && a.origin==b.origin && a.provenance==b.provenance
            && a.componentAnchorZoneId==b.componentAnchorZoneId
            && a.mprIdentity==b.mprIdentity && a.mprPath==b.mprPath && a.mobPath==b.mobPath
            && a.displayName==b.displayName && a.sectorWidth==b.sectorWidth && a.sectorHeight==b.sectorHeight;
    };
    QVector<SWorldMapZoneOrigin> candidate=m_zones;
    QSet<QString> changed;
    for(const auto& change : changes)
    {
        const auto& expected=forward?change.before:change.after;
        const auto& desired=forward?change.after:change.before;
        const bool hadExpected=forward?change.hadBefore:change.hasAfter;
        const bool hasDesired=forward?change.hasAfter:change.hadBefore;
        const auto& identity=hadExpected?expected:desired;
        const QString id=zoneKey(identity.profileId,identity.allodId,identity.zoneId);
        if((!hadExpected && !hasDesired) || changed.contains(id)
           || (hasDesired && (!validZone(desired,error)
               || id!=zoneKey(desired.profileId,desired.allodId,desired.zoneId))))
        { if(error && error->isEmpty()) *error=QStringLiteral("Invalid or duplicate world-layout change."); return false; }
        changed.insert(id);
        int index=-1;
        for(int i=0;i<candidate.size();++i)
            if(zoneKey(candidate[i].profileId,candidate[i].allodId,candidate[i].zoneId)==id) {index=i;break;}
        // Resource dimensions are verified asynchronously from MPR headers.
        // An origin-only command must retain that metadata, including on Undo.
        const bool unchangedSize=change.before.sectorWidth==change.after.sectorWidth
            && change.before.sectorHeight==change.after.sectorHeight;
        auto comparable=expected;
        auto next=desired;
        if(index>=0 && unchangedSize)
        {
            comparable.sectorWidth=next.sectorWidth=candidate[index].sectorWidth;
            comparable.sectorHeight=next.sectorHeight=candidate[index].sectorHeight;
        }
        if((index>=0)!=hadExpected || (hadExpected && !same(candidate[index],comparable)))
        { if(error) *error=QString::fromUtf8("Раскладка карты изменилась. Перезагрузите мир перед повторным действием или отменой."); return false; }
        if(index<0 && hasDesired)
        {
            const QString full=absoluteResourcePath(next.mprPath);
            if(next.mprPath.isEmpty() || next.sectorWidth<=0 || next.sectorHeight<=0
               || !QFileInfo(full).isFile()
               || (!next.mobPath.isEmpty() && !QFileInfo(absoluteResourcePath(next.mobPath)).isFile()))
            { if(error) *error=QStringLiteral("A selected map file is missing or its dimensions are invalid."); return false; }
            const QString physical=key(QFileInfo(full).canonicalFilePath());
            for(const auto& existing:candidate)
                if(key(existing.profileId)==key(next.profileId)
                   && (key(existing.zoneId)==key(next.zoneId)
                       || (!existing.mprPath.isEmpty()
                           && key(QFileInfo(absoluteResourcePath(existing.mprPath)).canonicalFilePath())==physical)))
                { if(error) *error=QStringLiteral("This map is already in the profile world layout."); return false; }
            next.mprPath=portableResourcePath(full);
            next.mobPath=portableResourcePath(absoluteResourcePath(next.mobPath));
        }
        if(index>=0 && hasDesired) candidate[index]=next;
        else if(index>=0) candidate.remove(index);
        else if(hasDesired) candidate.append(next);
    }
    if(changes.isEmpty()) return true;
    QVector<SWorldMapZoneOrigin> checked;
    const QByteArray bytes=serialize(candidate,error);
    if(bytes.isEmpty() || !parse(bytes,&checked,error)) return false;
    if(!save(candidate,error)) return false;
    m_zones=candidate; m_hasPersistentFile=true;
    return true;
}
