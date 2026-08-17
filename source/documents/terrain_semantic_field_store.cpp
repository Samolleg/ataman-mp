#include "terrain_semantic_field_store.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include "endurance_document.h"

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
    if(schemaVersion != 1 || mprNamespace.trimmed().isEmpty()
       || mprSha256.size() != 32 || atlasPixelSha256.size() != 32
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
    root.insert(QStringLiteral("mprSha256"),
                QString::fromLatin1(mprSha256.toHex()));
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
    parsed.atlasPixelSha256 = parseHash(root,
                                        QStringLiteral("atlasPixelSha256"));
    parsed.profileSha256 = parseHash(root, QStringLiteral("profileSha256"));
    parsed.vertexWidth = root.value(QStringLiteral("vertexWidth")).toInt();
    parsed.vertexHeight = root.value(QStringLiteral("vertexHeight")).toInt();
    const int count = parsed.vertexWidth * parsed.vertexHeight;
    if(root.value(QStringLiteral("schema")).toString()
            != QString::fromLatin1(kSchema)
       || parsed.schemaVersion != 1 || parsed.vertexWidth <= 0
       || parsed.vertexHeight <= 0 || parsed.mprNamespace.trimmed().isEmpty()
       || parsed.mprSha256.size() != 32
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
    int expectedVertexHeight, CValidationReport* report) const
{
    const bool valid = mprNamespace.compare(expectedNamespace,
        Qt::CaseInsensitive) == 0 && mprSha256 == expectedMprSha256
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
