#include "hot_reload_protocol.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>
#include <QtGlobal>

namespace {
bool isOwnedPath(const QString& path, const QString& root)
{
    const QString cleanPath = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QString cleanRoot = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(root).absoluteFilePath()));
    if(!cleanRoot.endsWith('/')) cleanRoot += '/';
    return cleanPath.startsWith(cleanRoot, Qt::CaseInsensitive);
}
}

QJsonObject mapHotReloadRequestToJson(const SMapHotReloadRequest& request)
{
    QJsonObject object;
    object.insert("protocolVersion", request.protocolVersion);
    object.insert("sequence", QString::number(request.sequence));
    object.insert("generation", QString::number(request.generation));
    object.insert("runtimeStateSequence", QString::number(request.runtimeStateSequence));
    object.insert("runId", request.runId);
    object.insert("profileId", request.profileId);
    object.insert("sourceZoneId", request.sourceZoneId);
    object.insert("bootstrapMprName", request.bootstrapMprName);
    object.insert("bootstrapMobName", request.bootstrapMobName);
    object.insert("zoneId", request.zoneId);
    object.insert("bootstrapMode", request.bootstrapMode);
    object.insert("entranceId", request.entranceId);
    object.insert("mprPath", request.mprPath);
    object.insert("mobPath", request.mobPath);
    object.insert("mprSha256", request.mprSha256);
    object.insert("mobSha256", request.mobSha256);
    object.insert("bootstrapMobPath", request.bootstrapMobPath);
    object.insert("bootstrapMobSha256", request.bootstrapMobSha256);
    object.insert("minimapTextureName", request.minimapTextureName);
    object.insert("minimapResourcePath", request.minimapResourcePath);
    object.insert("minimapResourceSha256", request.minimapResourceSha256);
    object.insert("savePath", request.savePath);
    object.insert("preservePosition", request.preservePosition);
    object.insert("graphless", request.graphless);
    object.insert("syntheticMapIdentity", request.syntheticMapIdentity);
    object.insert("mapSizeX", request.mapSizeX);
    object.insert("mapSizeY", request.mapSizeY);
    object.insert("spawnX", request.spawnX);
    object.insert("spawnY", request.spawnY);
    return object;
}

bool mapHotReloadRequestFromJson(const QJsonObject& object,
                                 SMapHotReloadRequest* request,
                                 QStringList* errors)
{
    if(!request) return false;
    SMapHotReloadRequest parsed;
    parsed.protocolVersion = object.value("protocolVersion").toInt();
    bool sequenceValid = false;
    parsed.sequence = object.value("sequence").toString().toULongLong(&sequenceValid);
    bool generationValid = false;
    bool stateSequenceValid = false;
    parsed.generation = object.value("generation").toString().toULongLong(&generationValid);
    parsed.runtimeStateSequence = object.value("runtimeStateSequence").toString()
        .toULongLong(&stateSequenceValid);
    parsed.runId = object.value("runId").toString();
    parsed.profileId = object.value("profileId").toString();
    parsed.sourceZoneId = object.value("sourceZoneId").toString();
    parsed.bootstrapMprName = object.value("bootstrapMprName").toString();
    parsed.bootstrapMobName = object.value("bootstrapMobName").toString();
    parsed.zoneId = object.value("zoneId").toString();
    parsed.bootstrapMode = object.value("bootstrapMode").toString();
    parsed.entranceId = object.value("entranceId").toInt();
    parsed.mprPath = object.value("mprPath").toString();
    parsed.mobPath = object.value("mobPath").toString();
    parsed.mprSha256 = object.value("mprSha256").toString();
    parsed.mobSha256 = object.value("mobSha256").toString();
    parsed.bootstrapMobPath = object.value("bootstrapMobPath").toString();
    parsed.bootstrapMobSha256 = object.value("bootstrapMobSha256").toString();
    parsed.minimapTextureName = object.value("minimapTextureName").toString();
    parsed.minimapResourcePath = object.value("minimapResourcePath").toString();
    parsed.minimapResourceSha256 = object.value("minimapResourceSha256").toString();
    parsed.savePath = object.value("savePath").toString();
    parsed.preservePosition = object.value("preservePosition").toBool();
    parsed.graphless = object.value("graphless").toBool();
    parsed.syntheticMapIdentity = object.value("syntheticMapIdentity").toBool();
    parsed.mapSizeX = object.value("mapSizeX").toInt();
    parsed.mapSizeY = object.value("mapSizeY").toInt();
    parsed.spawnX = object.value("spawnX").toDouble();
    parsed.spawnY = object.value("spawnY").toDouble();
    if(!sequenceValid && errors) errors->append("Hot-reload sequence is invalid.");
    if(!generationValid && errors) errors->append("Hot-reload generation is invalid.");
    if(!stateSequenceValid && errors) errors->append("Runtime-state sequence is invalid.");
    *request = parsed;
    return sequenceValid && generationValid && stateSequenceValid;
}

bool validateMapHotReloadRequest(const SMapHotReloadRequest& request,
                                 const QString& ownedRunRoot,
                                 QStringList* errors)
{
    QStringList local;
    if(request.protocolVersion != 4) local.append("Unsupported hot-reload protocol version.");
    if(request.sequence == 0) local.append("Hot-reload sequence must be positive.");
    if(request.generation == 0) local.append("Hot-reload generation must be positive.");
    if(request.runId.isEmpty() || request.profileId.isEmpty() || request.zoneId.isEmpty())
        local.append("Run, profile, and zone identities are required.");
    if(request.sourceZoneId.isEmpty() || request.bootstrapMprName.isEmpty()
       || request.bootstrapMobName.isEmpty())
        local.append("The source zone and bootstrap MPR/MOB identities are required.");
    if(request.entranceId <= 0) local.append("A valid map-table entrance is required.");
    if(!isOwnedPath(request.mprPath, ownedRunRoot)
       || !isOwnedPath(request.mobPath, ownedRunRoot)
       || !isOwnedPath(request.savePath, ownedRunRoot))
        local.append("Hot-reload payloads must remain inside the owned test-run directory.");
    if(!request.bootstrapMobPath.isEmpty()
       && !isOwnedPath(request.bootstrapMobPath, ownedRunRoot))
        local.append("The hot-reload bootstrap MOB must remain inside the owned test-run directory.");
    if(!request.minimapResourcePath.isEmpty()
       && !isOwnedPath(request.minimapResourcePath, ownedRunRoot))
        local.append("The hot-reload minimap RES must remain inside the owned test-run directory.");
    if(!QFileInfo::exists(request.mprPath) || !QFileInfo::exists(request.mobPath))
        local.append("Hot-reload payload files are missing.");
    if(!QFileInfo(request.savePath).isDir())
        local.append("The generation save directory is missing.");
    else if(!QDir(request.savePath).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
        local.append("A new hot-reload save generation must be empty.");
    if(request.bootstrapMode != "automatic" && request.bootstrapMode != "transition"
       && request.bootstrapMode != "new-game")
        local.append("Unsupported hot-reload bootstrap mode.");
    if(request.syntheticMapIdentity
       && (request.mapSizeX <= 0 || request.mapSizeY <= 0
           || request.mapSizeX > 1024 || request.mapSizeY > 1024
           || !qIsFinite(request.spawnX) || !qIsFinite(request.spawnY)
           || request.sourceZoneId.isEmpty()
           || request.bootstrapMprName.isEmpty()
           || request.bootstrapMobName.isEmpty()))
        local.append("Synthetic-map dimensions or entrance coordinates are invalid.");
    if(!request.bootstrapMobPath.isEmpty() && !QFileInfo::exists(request.bootstrapMobPath))
        local.append("The hot-reload bootstrap MOB is missing.");
    const QRegularExpression textureName("^[A-Za-z0-9_.-]+$");
    if(!request.minimapTextureName.isEmpty()
       && !textureName.match(request.minimapTextureName).hasMatch())
        local.append("The hot-reload minimap texture name is unsafe.");
    if(!request.minimapResourcePath.isEmpty()
       && (request.minimapTextureName.isEmpty()
           || !QFileInfo::exists(request.minimapResourcePath)))
        local.append("The hot-reload minimap RES or texture identity is missing.");
    const QRegularExpression hash("^[0-9a-f]{64}$", QRegularExpression::CaseInsensitiveOption);
    if(!hash.match(request.mprSha256).hasMatch()
       || !hash.match(request.mobSha256).hasMatch())
        local.append("Hot-reload payload hashes are invalid.");
    if(!request.bootstrapMobPath.isEmpty()
       && !hash.match(request.bootstrapMobSha256).hasMatch())
        local.append("The hot-reload bootstrap MOB hash is invalid.");
    if(!request.minimapResourcePath.isEmpty()
       && !hash.match(request.minimapResourceSha256).hasMatch())
        local.append("The hot-reload minimap RES hash is invalid.");
    if(errors) errors->append(local);
    return local.isEmpty();
}

QByteArray mapHotReloadRequestToControlBytes(const SMapHotReloadRequest& request)
{
    QByteArray bytes;
    QTextStream stream(&bytes, QIODevice::WriteOnly);
    stream.setCodec("UTF-8");
    const auto line = [&stream](const QString& key, QString value)
    {
        value.replace('\r', ' ');
        value.replace('\n', ' ');
        stream << key << '=' << value << '\n';
    };
    line("protocol", QString::number(request.protocolVersion));
    line("sequence", QString::number(request.sequence));
    line("generation", QString::number(request.generation));
    line("runtimeStateSequence", QString::number(request.runtimeStateSequence));
    line("runId", request.runId);
    line("profile", request.profileId);
    line("sourceZone", request.sourceZoneId);
    line("bootstrapMpr", request.bootstrapMprName);
    line("bootstrapMobName", request.bootstrapMobName);
    line("zone", request.zoneId);
    line("bootstrapMode", request.bootstrapMode);
    line("entrance", QString::number(request.entranceId));
    line("mpr", QDir::toNativeSeparators(request.mprPath));
    line("mob", QDir::toNativeSeparators(request.mobPath));
    line("mprSha256", request.mprSha256.toLower());
    line("mobSha256", request.mobSha256.toLower());
    line("bootstrapMob", QDir::toNativeSeparators(request.bootstrapMobPath));
    line("bootstrapMobSha256", request.bootstrapMobSha256.toLower());
    line("minimapTexture", request.minimapTextureName);
    line("minimapResource", QDir::toNativeSeparators(request.minimapResourcePath));
    line("minimapResourceSha256", request.minimapResourceSha256.toLower());
    line("savePath", QDir::toNativeSeparators(request.savePath));
    line("preservePosition", request.preservePosition ? "1" : "0");
    line("graphless", request.graphless ? "1" : "0");
    line("syntheticMapIdentity", request.syntheticMapIdentity ? "1" : "0");
    line("mapSizeX", QString::number(request.mapSizeX));
    line("mapSizeY", QString::number(request.mapSizeY));
    line("spawnX", QString::number(request.spawnX, 'f', 6));
    line("spawnY", QString::number(request.spawnY, 'f', 6));
    stream.flush();
    return bytes;
}

bool mapHotReloadRequestFromControlBytes(const QByteArray& bytes,
                                         SMapHotReloadRequest* request,
                                         QStringList* errors)
{
    if(!request) return false;
    QHash<QString, QString> values;
    const QString text = QString::fromUtf8(bytes);
    for(const QString& rawLine : text.split('\n'))
    {
        const QString line = rawLine.trimmed();
        if(line.isEmpty() || line.startsWith('#')) continue;
        const int separator = line.indexOf('=');
        if(separator <= 0)
        {
            if(errors) errors->append("Hot-reload control line is not key=value.");
            return false;
        }
        values.insert(line.left(separator).trimmed(), line.mid(separator + 1).trimmed());
    }
    SMapHotReloadRequest parsed;
    bool protocolOk = false;
    bool sequenceOk = false;
    bool generationOk = false;
    bool stateSequenceOk = false;
    bool entranceOk = false;
    parsed.protocolVersion = values.value("protocol").toInt(&protocolOk);
    parsed.sequence = values.value("sequence").toULongLong(&sequenceOk);
    parsed.generation = values.value("generation").toULongLong(&generationOk);
    parsed.runtimeStateSequence = values.value("runtimeStateSequence")
        .toULongLong(&stateSequenceOk);
    parsed.runId = values.value("runId");
    parsed.profileId = values.value("profile");
    parsed.sourceZoneId = values.value("sourceZone");
    parsed.bootstrapMprName = values.value("bootstrapMpr");
    parsed.bootstrapMobName = values.value("bootstrapMobName");
    parsed.zoneId = values.value("zone");
    parsed.bootstrapMode = values.value("bootstrapMode");
    parsed.entranceId = values.value("entrance").toInt(&entranceOk);
    parsed.mprPath = QDir::fromNativeSeparators(values.value("mpr"));
    parsed.mobPath = QDir::fromNativeSeparators(values.value("mob"));
    parsed.mprSha256 = values.value("mprSha256");
    parsed.mobSha256 = values.value("mobSha256");
    parsed.bootstrapMobPath = QDir::fromNativeSeparators(values.value("bootstrapMob"));
    parsed.bootstrapMobSha256 = values.value("bootstrapMobSha256");
    parsed.minimapTextureName = values.value("minimapTexture");
    parsed.minimapResourcePath = QDir::fromNativeSeparators(values.value("minimapResource"));
    parsed.minimapResourceSha256 = values.value("minimapResourceSha256");
    parsed.savePath = QDir::fromNativeSeparators(values.value("savePath"));
    parsed.preservePosition = values.value("preservePosition") == "1";
    parsed.graphless = values.value("graphless") == "1";
    parsed.syntheticMapIdentity = values.value("syntheticMapIdentity") == "1";
    bool mapSizeXOk = false;
    bool mapSizeYOk = false;
    bool spawnXOk = false;
    bool spawnYOk = false;
    parsed.mapSizeX = values.value("mapSizeX").toInt(&mapSizeXOk);
    parsed.mapSizeY = values.value("mapSizeY").toInt(&mapSizeYOk);
    parsed.spawnX = values.value("spawnX").toDouble(&spawnXOk);
    parsed.spawnY = values.value("spawnY").toDouble(&spawnYOk);
    if(!parsed.syntheticMapIdentity)
        mapSizeXOk = mapSizeYOk = spawnXOk = spawnYOk = true;
    if(!protocolOk || !sequenceOk || !generationOk || !stateSequenceOk || !entranceOk
       || !mapSizeXOk || !mapSizeYOk || !spawnXOk || !spawnYOk)
    {
        if(errors) errors->append("Hot-reload control numeric fields are invalid.");
        return false;
    }
    *request = parsed;
    return true;
}
