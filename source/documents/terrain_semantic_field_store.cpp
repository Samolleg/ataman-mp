#include "terrain_semantic_field_store.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCodec>
#include <QtEndian>
#include <algorithm>
#include <limits>

#include "endurance_document.h"
#include "mpr_document.h"

namespace {
const char kSchema[] = "ei-mapper-endurance-terrain-semantic-field";

QByteArray fieldBytes(const QVector<qint32>& values)
{
    QByteArray bytes(values.size() * int(sizeof(qint32)), Qt::Uninitialized);
    for(int i = 0; i < values.size(); ++i)
        qToLittleEndian<qint32>(values.at(i),
            reinterpret_cast<uchar*>(bytes.data() + i * int(sizeof(qint32))));
    return bytes;
}

QJsonObject encodedField(const QVector<qint32>& values)
{
    const QByteArray raw = fieldBytes(values);
    QJsonObject result;
    result.insert(QStringLiteral("count"), values.size());
    result.insert(QStringLiteral("sha256"), QString::fromLatin1(
        QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex()));
    result.insert(QStringLiteral("qcompressBase64"), QString::fromLatin1(
        qCompress(raw, 9).toBase64()));
    return result;
}

bool decodeField(const QJsonObject& object, int expectedCount,
                 QVector<qint32>* values, CValidationReport* report,
                 const QString& location)
{
    if(!values) return false;
    const int count = object.value(QStringLiteral("count")).toInt(-1);
    const QByteArray compressed = QByteArray::fromBase64(
        object.value(QStringLiteral("qcompressBase64")).toString().toLatin1());
    const QByteArray raw = qUncompress(compressed);
    const QByteArray expectedHash = object.value(QStringLiteral("sha256"))
        .toString().toLatin1().toLower();
    const QByteArray actualHash = QCryptographicHash::hash(
        raw, QCryptographicHash::Sha256).toHex();
    const bool validCount = count == 0 || count == expectedCount;
    if(!validCount || raw.size() != count * int(sizeof(qint32))
       || expectedHash.size() != 64 || expectedHash != actualHash)
    {
        if(report) report->add(QStringLiteral("terrain.semantic_field_payload"),
            EValidationSeverity::Error,
            QStringLiteral("The stored shared-corner material field is incomplete or corrupt"),
            location);
        return false;
    }
    values->resize(count);
    for(int i = 0; i < count; ++i)
        (*values)[i] = qFromLittleEndian<qint32>(
            reinterpret_cast<const uchar*>(raw.constData()
                + i * int(sizeof(qint32))));
    return true;
}

QByteArray parseHash(const QJsonObject& object, const QString& key)
{
    return QByteArray::fromHex(object.value(key).toString().toLatin1());
}
}

QByteArray STerrainSemanticFieldDocument::toBytes(CValidationReport* report) const
{
    const int expectedCount = vertexWidth * vertexHeight;
    if((schemaVersion != 1 && schemaVersion != 2) || mprNamespace.trimmed().isEmpty()
       || (schemaVersion == 1 ? mprSha256.size() != 32
                             : terrainTilesSha256.size() != 32)
       || atlasPixelSha256.size() != 32
       || profileSha256.size() != 32
       || vertexWidth <= 0 || vertexHeight <= 0
       || (!land.isEmpty() && land.size() != expectedCount)
       || (!overlay.isEmpty() && overlay.size() != expectedCount))
    {
        if(report) report->add(QStringLiteral("terrain.semantic_field_document"),
            EValidationSeverity::Error,
            QStringLiteral("The shared-corner material field cannot be serialized because its binding or dimensions are incomplete"));
        return QByteArray();
    }
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(kSchema));
    root.insert(QStringLiteral("version"), schemaVersion);
    root.insert(QStringLiteral("cornerSpace"),
                QStringLiteral("grid-nw-ne-se-sw"));
    root.insert(QStringLiteral("mprNamespace"), mprNamespace);
    if(schemaVersion == 1)
        root.insert(QStringLiteral("mprSha256"),
                    QString::fromLatin1(mprSha256.toHex()));
    else
        root.insert(QStringLiteral("terrainTilesSha256"),
                    QString::fromLatin1(terrainTilesSha256.toHex()));
    root.insert(QStringLiteral("atlasPixelSha256"),
                QString::fromLatin1(atlasPixelSha256.toHex()));
    root.insert(QStringLiteral("profileSha256"),
                QString::fromLatin1(profileSha256.toHex()));
    root.insert(QStringLiteral("vertexWidth"), vertexWidth);
    root.insert(QStringLiteral("vertexHeight"), vertexHeight);
    root.insert(QStringLiteral("land"), encodedField(land));
    root.insert(QStringLiteral("overlay"), encodedField(overlay));
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool STerrainSemanticFieldDocument::fromBytes(
    const QByteArray& bytes, STerrainSemanticFieldDocument* document,
    CValidationReport* report, const QString& sourceName)
{
    if(!document) return false;
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if(parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        if(report) report->add(QStringLiteral("terrain.semantic_field_json"),
            EValidationSeverity::Error,
            QStringLiteral("The stored shared-corner material field is not valid JSON: %1")
                .arg(parseError.errorString()), sourceName);
        return false;
    }
    const QJsonObject root = json.object();
    STerrainSemanticFieldDocument parsed;
    parsed.schemaVersion = root.value(QStringLiteral("version")).toInt(-1);
    parsed.mprNamespace = root.value(QStringLiteral("mprNamespace")).toString();
    parsed.mprSha256 = parseHash(root, QStringLiteral("mprSha256"));
    parsed.terrainTilesSha256 = parseHash(root, QStringLiteral("terrainTilesSha256"));
    parsed.atlasPixelSha256 = parseHash(root,
                                        QStringLiteral("atlasPixelSha256"));
    parsed.profileSha256 = parseHash(root, QStringLiteral("profileSha256"));
    parsed.vertexWidth = root.value(QStringLiteral("vertexWidth")).toInt();
    parsed.vertexHeight = root.value(QStringLiteral("vertexHeight")).toInt();
    const int count = parsed.vertexWidth * parsed.vertexHeight;
    if(root.value(QStringLiteral("schema")).toString()
            != QString::fromLatin1(kSchema)
       || (parsed.schemaVersion != 1 && parsed.schemaVersion != 2) || parsed.vertexWidth <= 0
       || parsed.vertexHeight <= 0 || parsed.mprNamespace.trimmed().isEmpty()
       || (parsed.schemaVersion == 1 ? parsed.mprSha256.size() != 32
                                    : parsed.terrainTilesSha256.size() != 32)
       || parsed.atlasPixelSha256.size() != 32
       || parsed.profileSha256.size() != 32
       || !decodeField(root.value(QStringLiteral("land")).toObject(), count,
                       &parsed.land, report, sourceName + QStringLiteral(":land"))
       || !decodeField(root.value(QStringLiteral("overlay")).toObject(), count,
                       &parsed.overlay, report,
                       sourceName + QStringLiteral(":overlay")))
    {
        if(report && !report->hasErrors())
            report->add(QStringLiteral("terrain.semantic_field_schema"),
                EValidationSeverity::Error,
                QStringLiteral("The stored shared-corner material field has an unsupported schema or incomplete binding"),
                sourceName);
        return false;
    }
    *document = parsed;
    return true;
}

bool STerrainSemanticFieldDocument::validateBinding(
    const QString& expectedNamespace, const QByteArray& expectedMprSha256,
    const QByteArray& expectedAtlasPixelSha256,
    const QByteArray& expectedProfileSha256, int expectedVertexWidth,
    int expectedVertexHeight, CValidationReport* report,
    const QByteArray& expectedTerrainTilesSha256) const
{
    const bool mapBinding = schemaVersion == 1
        ? mprSha256.size() == 32 && mprSha256 == expectedMprSha256
        : schemaVersion == 2 && terrainTilesSha256.size() == 32
            && terrainTilesSha256 == expectedTerrainTilesSha256;
    const bool valid = mprNamespace.compare(expectedNamespace,
        Qt::CaseInsensitive) == 0 && mapBinding
        && atlasPixelSha256 == expectedAtlasPixelSha256
        && profileSha256 == expectedProfileSha256
        && vertexWidth == expectedVertexWidth
        && vertexHeight == expectedVertexHeight
        && (land.isEmpty() || land.size() == vertexWidth * vertexHeight)
        && (overlay.isEmpty() || overlay.size() == vertexWidth * vertexHeight);
    if(!valid && report)
        report->add(QStringLiteral("terrain.semantic_field_binding"),
            EValidationSeverity::Error,
            QStringLiteral("The stored shared-corner material field does not belong to this exact MPR and terrain atlas"));
    return valid;
}

QByteArray STerrainSemanticFieldDocument::tileBindingSha256(const CMprDocument& mpr)
{
    if(!mpr.mapInfo().valid) return {};
    QByteArray mp = mpr.memberPayload(mpr.mapInfo().memberName);
    if(mp.size() < 38) return {};
    mp.replace(4, 4, QByteArray(4, '\0')); // maxZ is geometry, not painting.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData("ei-terrain-tile-binding-v2", 26);
    hash.addData(mp);
    auto sectors = mpr.sectors();
    std::sort(sectors.begin(), sectors.end(), [](const SMprSectorInfo& a,
                                                const SMprSectorInfo& b) {
        return a.sectorY != b.sectorY ? a.sectorY < b.sectorY : a.sectorX < b.sectorX;
    });
    for(const auto& sector : sectors) {
        QByteArray identity(9, '\0');
        qToLittleEndian<qint32>(sector.sectorX, reinterpret_cast<uchar*>(identity.data()));
        qToLittleEndian<qint32>(sector.sectorY, reinterpret_cast<uchar*>(identity.data()+4));
        identity[8] = char(sector.hasOverlay);
        hash.addData(identity);
        const QByteArray payload = mpr.memberPayload(sector.memberName);
        hash.addData(payload.mid(int(sector.landTileOffset), 512));
        if(sector.hasOverlay) {
            hash.addData(payload.mid(int(sector.overlayTileOffset), 512));
            hash.addData(payload.mid(int(sector.overlayMaterialOffset), 512));
        }
    }
    return hash.result();
}

QString STerrainSemanticFieldDocument::embeddedMemberName()
{
    return QStringLiteral("ei_mapper_terrain_semantics.json");
}

bool STerrainSemanticFieldDocument::embedInMpr(
    const QByteArray& mprBytes, const QByteArray& semanticBytes,
    QByteArray* output, CValidationReport* report)
{
    if(!output) return false;
    output->clear();
    const auto refuse = [report](const QString& message) {
        if(report) report->add("terrain.semantic_field_embed", EValidationSeverity::Error, message);
        return false;
    };
    CMprDocument mpr;
    STerrainSemanticFieldDocument semantics;
    if(!mpr.loadBytes(mprBytes, QStringLiteral("semantic-field.mpr"), report)
       || !fromBytes(semanticBytes, &semantics, report)
       || semantics.schemaVersion != 2
       || semantics.mprNamespace.compare(mpr.mapInfo().innerNamespace, Qt::CaseInsensitive) != 0
       || semantics.terrainTilesSha256 != tileBindingSha256(mpr)
       || semantics.vertexWidth != int(mpr.mapInfo().sectorCountX)*16+1
       || semantics.vertexHeight != int(mpr.mapInfo().sectorCountY)*16+1)
        return refuse(QStringLiteral("The embedded material field does not match the MPR tile topology."));

    const QString ownedName = embeddedMemberName();
    const QByteArray encodedName = ownedName.toLatin1();
    int existing = -1;
    for(int i=0; i<mpr.members().size(); ++i) {
        if(mpr.members()[i].name.compare(ownedName, Qt::CaseInsensitive) != 0) continue;
        if(existing >= 0) return refuse(QStringLiteral("The embedded material member is ambiguous."));
        existing = i;
    }
    if(existing >= 0) {
        STerrainSemanticFieldDocument old;
        if(!fromBytes(mpr.members()[existing].payload, &old, report))
            return refuse(QStringLiteral("The editor member name belongs to an unrecognized payload; it was preserved."));
        if(mpr.members()[existing].payload == semanticBytes) { *output = mprBytes; return true; }
    }

    const auto read32 = [&mprBytes](int offset) {
        return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(mprBytes.constData()+offset));
    };
    const int oldCount = mpr.members().size();
    const int count = oldCount + (existing < 0 ? 1 : 0);
    const int oldTable = int(read32(8));
    const int oldNamesOffset = oldTable + oldCount*22;
    const int oldNamesSize = int(read32(12));
    QByteArray names = mprBytes.mid(oldNamesOffset, oldNamesSize);
    const QByteArray trailer = mprBytes.mid(oldNamesOffset + oldNamesSize);
    const quint64 needed = quint64(mprBytes.size()) + semanticBytes.size()
        + quint64(count)*22 + names.size() + trailer.size() + encodedName.size() + 32;
    if(needed > quint64(std::numeric_limits<int>::max()))
        return refuse(QStringLiteral("The embedded material member exceeds the archive size limit."));
    QByteArray candidate = mprBytes;
    candidate.append(QByteArray((16-candidate.size()%16)%16, '\0'));
    const quint32 payloadOffset = quint32(candidate.size());
    candidate.append(semanticBytes);
    candidate.append(QByteArray((16-candidate.size()%16)%16, '\0'));
    const quint32 tableOffset = quint32(candidate.size());
    QVector<QByteArray> entries;
    QVector<QByteArray> entryNames;
    for(int i=0; i<oldCount; ++i) {
        QByteArray entry = mprBytes.mid(oldTable+i*22, 22);
        if(i == existing) {
            qToLittleEndian<quint32>(quint32(semanticBytes.size()), reinterpret_cast<uchar*>(entry.data()+4));
            qToLittleEndian<quint32>(payloadOffset, reinterpret_cast<uchar*>(entry.data()+8));
        }
        entries.append(entry); entryNames.append(mpr.members()[i].encodedName);
    }
    if(existing < 0) {
        QByteArray entry(22, '\0');
        qToLittleEndian<quint32>(quint32(semanticBytes.size()), reinterpret_cast<uchar*>(entry.data()+4));
        qToLittleEndian<quint32>(payloadOffset, reinterpret_cast<uchar*>(entry.data()+8));
        qToLittleEndian<quint16>(quint16(encodedName.size()), reinterpret_cast<uchar*>(entry.data()+16));
        qToLittleEndian<quint32>(quint32(names.size()), reinterpret_cast<uchar*>(entry.data()+18));
        names.append(encodedName); entries.append(entry); entryNames.append(encodedName);
    }
    QVector<int> tableSlots(count, -1);
    QVector<quint32> next(count, quint32(-1));
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    int lastFree = count-1;
    for(int i=0; i<count; ++i) {
        const QByteArray lowered = codec ? codec->fromUnicode(codec->toUnicode(entryNames[i]).toLower())
                                         : entryNames[i].toLower();
        quint32 sum=0; for(unsigned char byte : lowered) sum += byte;
        int slot = int(sum % quint32(count));
        if(tableSlots[slot] >= 0) {
            while(next[slot] != quint32(-1)) slot=int(next[slot]);
            while(lastFree >= 0 && tableSlots[lastFree] >= 0) --lastFree;
            if(lastFree < 0) return refuse(QStringLiteral("The material member hash table is full."));
            next[slot]=quint32(lastFree); slot=lastFree--;
        }
        tableSlots[slot]=i;
    }
    for(int slot=0; slot<count; ++slot) {
        if(tableSlots[slot] < 0) return refuse(QStringLiteral("The material member hash table is incomplete."));
        QByteArray entry=entries[tableSlots[slot]];
        qToLittleEndian<quint32>(next[slot], reinterpret_cast<uchar*>(entry.data()));
        candidate.append(entry);
    }
    candidate.append(names); candidate.append(trailer);
    qToLittleEndian<quint32>(quint32(count), reinterpret_cast<uchar*>(candidate.data()+4));
    qToLittleEndian<quint32>(tableOffset, reinterpret_cast<uchar*>(candidate.data()+8));
    qToLittleEndian<quint32>(quint32(names.size()), reinterpret_cast<uchar*>(candidate.data()+12));
    CMprDocument reopened;
    if(!reopened.loadBytes(candidate, QStringLiteral("embedded-semantic-field.mpr"), report)
       || reopened.memberPayload(ownedName) != semanticBytes
       || tileBindingSha256(reopened) != semantics.terrainTilesSha256)
        return refuse(QStringLiteral("The embedded material candidate failed fresh reopen."));
    for(const auto& before : mpr.members()) {
        if(before.name.compare(ownedName, Qt::CaseInsensitive) == 0) continue;
        const auto after = std::find_if(reopened.members().cbegin(), reopened.members().cend(),
            [&before](const SMprArchiveMember& member) { return member.encodedName == before.encodedName; });
        if(after == reopened.members().cend() || after->payload != before.payload
           || after->dataOffset != before.dataOffset || after->lastWriteTime != before.lastWriteTime)
            return refuse(QStringLiteral("Embedding materials changed an unrelated MPR member."));
    }
    *output = candidate;
    return true;
}

QString STerrainSemanticFieldDocument::memberName(
    const QString& mprNamespace, const QFileInfo& mprFile)
{
    QString readable = mprNamespace.trimmed().toCaseFolded();
    if(readable.isEmpty()) readable = mprFile.completeBaseName().toCaseFolded();
    for(int i = 0; i < readable.size(); ++i)
        if(!readable.at(i).isLetterOrNumber()) readable[i] = QLatin1Char('_');
    while(readable.contains(QStringLiteral("__")))
        readable.replace(QStringLiteral("__"), QStringLiteral("_"));
    readable = readable.left(32).trimmed();
    const QByteArray identity = (mprNamespace.trimmed().toCaseFolded()
        + QLatin1Char('|') + mprFile.completeBaseName().trimmed().toCaseFolded())
        .toUtf8();
    const QByteArray suffix = QCryptographicHash::hash(
        identity, QCryptographicHash::Sha256).toHex().left(16);
    return QStringLiteral("ei_terrain_semantics_%1_%2.json")
        .arg(readable, QString::fromLatin1(suffix));
}
