#include "terrain_atlas_recovery_store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>

#include "backup_manager.h"
#include "endurance_document.h"
#include "res_archive_document.h"
#include "terrain_atlas_profile.h"

namespace {
QByteArray sha256(const QByteArray& payload)
{
    return QCryptographicHash::hash(payload,
                                    QCryptographicHash::Sha256).toHex();
}

bool validSha256(const QByteArray& value)
{
    return QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$"))
        .match(QString::fromLatin1(value)).hasMatch();
}

QString safeIdentity(const QString& identity)
{
    const QString normalized = identity.trimmed().toLower();
    QString result = normalized;
    result.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")),
                   QStringLiteral("_"));
    result = result.left(64);
    if(result.isEmpty())
        result = QStringLiteral("atlas");
    return result + QStringLiteral("-") + QString::fromLatin1(
        sha256(normalized.toUtf8()).left(12));
}

bool writeAtomically(const QString& path, const QByteArray& bytes,
                     QString* error)
{
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly)
       || output.write(bytes) != bytes.size() || !output.commit())
    {
        if(error) *error = output.errorString();
        return false;
    }
    return true;
}
}

CTerrainAtlasRecoveryStore::CTerrainAtlasRecoveryStore(
    const QString& rootOverride)
    : m_rootPath(rootOverride.isEmpty()
        ? QDir(CBackupManager::defaultRootPath()).filePath(
              QStringLiteral("terrain-atlases"))
        : QDir::cleanPath(rootOverride))
{
}

QString CTerrainAtlasRecoveryStore::objectDirectory() const
{
    return QDir(m_rootPath).filePath(QStringLiteral("objects"));
}

QString CTerrainAtlasRecoveryStore::identityDirectory(
    const QString& resourceIdentity) const
{
    return QDir(m_rootPath).filePath(
        QStringLiteral("manifests/") + safeIdentity(resourceIdentity));
}

bool CTerrainAtlasRecoveryStore::writeObject(const QByteArray& payload,
                                             QByteArray* objectSha256,
                                             QString* error) const
{
    if(payload.isEmpty())
    {
        if(error) *error = QStringLiteral("A recovery object is empty.");
        return false;
    }
    const QByteArray hash = sha256(payload);
    if(objectSha256) *objectSha256 = hash;
    const QString directory = objectDirectory();
    if(!QDir().mkpath(directory))
    {
        if(error) *error = QStringLiteral("Cannot create recovery object directory: %1")
            .arg(directory);
        return false;
    }
    const QString path = QDir(directory).filePath(
        QString::fromLatin1(hash) + QStringLiteral(".blob"));
    if(QFileInfo::exists(path))
    {
        QFile existing(path);
        if(existing.open(QIODevice::ReadOnly)
           && sha256(existing.readAll()) == hash)
            return true;
        if(error) *error = QStringLiteral(
            "A content-addressed recovery object is corrupt: %1").arg(path);
        return false;
    }
    return writeAtomically(path, payload, error);
}

bool CTerrainAtlasRecoveryStore::readObject(const QByteArray& objectSha256,
                                            QByteArray* payload,
                                            QString* error) const
{
    if(!payload || !validSha256(objectSha256))
    {
        if(error) *error = QStringLiteral("A recovery object hash is invalid.");
        return false;
    }
    const QString path = QDir(objectDirectory()).filePath(
        QString::fromLatin1(objectSha256.toLower()) + QStringLiteral(".blob"));
    QFile input(path);
    if(!input.open(QIODevice::ReadOnly))
    {
        if(error) *error = QStringLiteral("A recovery object is missing: %1")
            .arg(path);
        return false;
    }
    const QByteArray bytes = input.readAll();
    if(sha256(bytes) != objectSha256.toLower())
    {
        if(error) *error = QStringLiteral("A recovery object failed its hash: %1")
            .arg(path);
        return false;
    }
    *payload = bytes;
    return true;
}

STerrainAtlasRecoveryResult CTerrainAtlasRecoveryStore::saveSnapshot(
    const STerrainAtlasRecoverySnapshot& snapshot) const
{
    STerrainAtlasRecoveryResult result;
    if(snapshot.resourceIdentity.trimmed().isEmpty()
       || !validSha256(snapshot.atlasPixelSha256)
       || !validSha256(snapshot.mprSha256)
       || snapshot.profileBytes.isEmpty() || snapshot.pages.isEmpty())
    {
        result.error = QStringLiteral(
            "The terrain-atlas recovery snapshot is incomplete.");
        return result;
    }
    QByteArray profileHash;
    if(!writeObject(snapshot.profileBytes, &profileHash, &result.error))
        return result;
    QJsonArray pageArray;
    for(const STerrainAtlasRecoveryPage& page : snapshot.pages)
    {
        if(page.name.trimmed().isEmpty())
        {
            result.error = QStringLiteral("A recovery atlas page has no name.");
            return result;
        }
        QByteArray pageHash;
        if(!writeObject(page.payload, &pageHash, &result.error))
            return result;
        QJsonObject object;
        object.insert(QStringLiteral("name"), page.name);
        object.insert(QStringLiteral("sha256"),
                      QString::fromLatin1(pageHash));
        pageArray.append(object);
    }
    QJsonArray metadataArray;
    for(const STerrainAtlasRecoveryPage& member : snapshot.metadataMembers)
    {
        if(member.name.trimmed().isEmpty() || member.payload.isEmpty())
        {
            result.error = QStringLiteral(
                "A recovery metadata member is incomplete.");
            return result;
        }
        QByteArray memberHash;
        if(!writeObject(member.payload, &memberHash, &result.error))
            return result;
        QJsonObject object;
        object.insert(QStringLiteral("name"), member.name);
        object.insert(QStringLiteral("sha256"),
                      QString::fromLatin1(memberHash));
        metadataArray.append(object);
    }
    QJsonArray runtimeTypes;
    for(qint32 type : snapshot.runtimeTerrainTypes)
        runtimeTypes.append(type);
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(QStringLiteral("createdUtc"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    manifest.insert(QStringLiteral("resourceIdentity"),
                    snapshot.resourceIdentity);
    manifest.insert(QStringLiteral("atlasPixelSha256"),
                    QString::fromLatin1(snapshot.atlasPixelSha256.toLower()));
    manifest.insert(QStringLiteral("profileObjectSha256"),
                    QString::fromLatin1(profileHash));
    manifest.insert(QStringLiteral("mprPath"), snapshot.mprPath);
    manifest.insert(QStringLiteral("resPath"), snapshot.resPath);
    manifest.insert(QStringLiteral("mprSha256"),
                    QString::fromLatin1(snapshot.mprSha256.toLower()));
    manifest.insert(QStringLiteral("runtimeTerrainTypes"), runtimeTypes);
    manifest.insert(QStringLiteral("pages"), pageArray);
    manifest.insert(QStringLiteral("metadataMembers"), metadataArray);

    const QString directory = identityDirectory(snapshot.resourceIdentity);
    if(!QDir().mkpath(directory))
    {
        result.error = QStringLiteral("Cannot create recovery manifest directory: %1")
            .arg(directory);
        return result;
    }
    const QString fileName = QStringLiteral("%1-%2.json")
        .arg(QString::fromLatin1(snapshot.atlasPixelSha256.toLower()),
             QString::fromLatin1(snapshot.mprSha256.toLower().left(16)));
    result.manifestPath = QDir(directory).filePath(fileName);
    if(QFileInfo::exists(result.manifestPath))
    {
        result.success = true;
        result.skipped = true;
        return result;
    }
    if(!writeAtomically(result.manifestPath,
                        QJsonDocument(manifest).toJson(QJsonDocument::Indented),
                        &result.error))
        return result;
    result.success = true;
    return result;
}

bool CTerrainAtlasRecoveryStore::findExact(
    const QString& resourceIdentity,
    const QByteArray& atlasPixelSha256,
    STerrainAtlasRecoverySnapshot* snapshot,
    QString* error) const
{
    if(!snapshot || resourceIdentity.trimmed().isEmpty()
       || !validSha256(atlasPixelSha256))
    {
        if(error) *error = QStringLiteral("The requested recovery identity is invalid.");
        return false;
    }
    const QDir directory(identityDirectory(resourceIdentity));
    const QFileInfoList manifests = directory.entryInfoList(
        {QString::fromLatin1(atlasPixelSha256.toLower())
             + QStringLiteral("-*.json")},
        QDir::Files, QDir::Time);
    for(const QFileInfo& info : manifests)
    {
        STerrainAtlasRecoverySnapshot candidate;
        QString manifestError;
        if(!loadManifest(info.absoluteFilePath(), &candidate,
                         &manifestError)
           || candidate.resourceIdentity != resourceIdentity
           || candidate.atlasPixelSha256 != atlasPixelSha256.toLower())
            continue;
        *snapshot = candidate;
        return true;
    }
    if(error) *error = QStringLiteral(
        "No exact terrain-atlas recovery snapshot was found for %1 (%2).")
        .arg(resourceIdentity, QString::fromLatin1(atlasPixelSha256));
    return false;
}

bool CTerrainAtlasRecoveryStore::findForMpr(
    const QString& resourceIdentity, const QByteArray& mprSha256,
    STerrainAtlasRecoverySnapshot* snapshot, QString* error) const
{
    if(!snapshot || resourceIdentity.trimmed().isEmpty()
       || !validSha256(mprSha256))
    {
        if(error) *error = QStringLiteral(
            "The requested recovery MPR identity is invalid.");
        return false;
    }
    const QDir directory(identityDirectory(resourceIdentity));
    const QFileInfoList manifests = directory.entryInfoList(
        {QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for(const QFileInfo& info : manifests)
    {
        STerrainAtlasRecoverySnapshot candidate;
        QString manifestError;
        if(!loadManifest(info.absoluteFilePath(), &candidate,
                         &manifestError)
           || candidate.resourceIdentity != resourceIdentity
           || candidate.mprSha256 != mprSha256.toLower())
            continue;
        *snapshot = candidate;
        return true;
    }
    if(error) *error = QStringLiteral(
        "No exact terrain-atlas recovery snapshot was found for MPR %1 (%2).")
        .arg(resourceIdentity, QString::fromLatin1(mprSha256));
    return false;
}

bool CTerrainAtlasRecoveryStore::buildStandaloneArchive(
    const STerrainAtlasRecoverySnapshot& snapshot,
    QByteArray* archiveBytes, QString* error) const
{
    if(!archiveBytes)
    {
        if(error) *error = QStringLiteral(
            "The recovery archive output is unavailable.");
        return false;
    }
    archiveBytes->clear();
    STerrainAtlasProfile profile;
    CValidationReport profileReport;
    if(!STerrainAtlasProfile::fromBytes(
           snapshot.profileBytes, &profile, &profileReport,
           QStringLiteral("terrain-atlas-recovery-profile"))
       || profile.mprNamespace != snapshot.resourceIdentity
       || profile.atlasPixelSha256 != snapshot.atlasPixelSha256.toLower()
       || profile.textureCount != snapshot.pages.size())
    {
        if(error) *error = profileReport.hasErrors()
            ? profileReport.toText().trimmed()
            : QStringLiteral(
                "The recovery profile does not match its atlas snapshot.");
        return false;
    }
    QVector<SResArchiveNewMember> members;
    QSet<QString> names;
    for(const STerrainAtlasRecoveryPage& page : snapshot.pages)
    {
        const QString normalized = page.name.trimmed().toLower();
        if(normalized.isEmpty() || page.payload.isEmpty()
           || names.contains(normalized))
        {
            if(error) *error = QStringLiteral(
                "The recovery snapshot contains an invalid or duplicate page.");
            return false;
        }
        names.insert(normalized);
        SResArchiveNewMember member;
        member.name = page.name;
        member.payload = page.payload;
        members.append(member);
    }
    SResArchiveNewMember profileMember;
    profileMember.name = CTerrainAtlasProfileStore::embeddedMemberName(
        profile.mprNamespace);
    profileMember.payload = snapshot.profileBytes;
    members.append(profileMember);
    for(const STerrainAtlasRecoveryPage& metadata : snapshot.metadataMembers)
    {
        const QString normalized = metadata.name.trimmed().toLower();
        if(normalized.isEmpty() || metadata.payload.isEmpty()
           || names.contains(normalized))
        {
            if(error) *error = QStringLiteral(
                "The recovery snapshot contains invalid or duplicate editor metadata.");
            return false;
        }
        names.insert(normalized);
        SResArchiveNewMember member;
        member.name = metadata.name;
        member.payload = metadata.payload;
        members.append(member);
    }

    QByteArray emptyArchive(16, '\0');
    qToLittleEndian<quint32>(0x019CE23C,
        reinterpret_cast<uchar*>(emptyArchive.data()));
    qToLittleEndian<quint32>(16,
        reinterpret_cast<uchar*>(emptyArchive.data() + 8));
    CResArchiveDocument empty;
    QStringList archiveErrors;
    if(!empty.loadBytes(emptyArchive, QStringLiteral("empty-recovery.res"),
                        &archiveErrors)
       || !empty.buildWithPublishedMembers(
           members, false, archiveBytes, &archiveErrors))
    {
        if(error) *error = archiveErrors.join(QStringLiteral("\n"));
        archiveBytes->clear();
        return false;
    }
    CResArchiveDocument reopened;
    if(!reopened.loadBytes(*archiveBytes,
                           QStringLiteral("rebuilt-recovery.res"),
                           &archiveErrors)
       || reopened.memberPayload(profileMember.name) != snapshot.profileBytes)
    {
        if(error) *error = archiveErrors.isEmpty()
            ? QStringLiteral("The rebuilt recovery RES failed exact reopen.")
            : archiveErrors.join(QStringLiteral("\n"));
        archiveBytes->clear();
        return false;
    }
    return true;
}

bool CTerrainAtlasRecoveryStore::loadManifest(
    const QString& manifestPath, STerrainAtlasRecoverySnapshot* snapshot,
    QString* error) const
{
    if(!snapshot)
        return false;
    QFile file(manifestPath);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error) *error = file.errorString();
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject manifest = document.object();
    STerrainAtlasRecoverySnapshot candidate;
    candidate.resourceIdentity = manifest.value(
        QStringLiteral("resourceIdentity")).toString();
    candidate.atlasPixelSha256 = manifest.value(
        QStringLiteral("atlasPixelSha256")).toString().toLatin1().toLower();
    candidate.mprPath = manifest.value(QStringLiteral("mprPath")).toString();
    candidate.resPath = manifest.value(QStringLiteral("resPath")).toString();
    candidate.mprSha256 = manifest.value(
        QStringLiteral("mprSha256")).toString().toLatin1().toLower();
    if(manifest.value(QStringLiteral("schemaVersion")).toInt() != 1
       || candidate.resourceIdentity.isEmpty()
       || !validSha256(candidate.atlasPixelSha256)
       || !validSha256(candidate.mprSha256))
    {
        if(error) *error = QStringLiteral(
            "A terrain-atlas recovery manifest is invalid: %1")
            .arg(manifestPath);
        return false;
    }
    const QByteArray profileHash = manifest.value(
        QStringLiteral("profileObjectSha256")).toString().toLatin1();
    if(!readObject(profileHash, &candidate.profileBytes, error))
        return false;
    for(const QJsonValue& value : manifest.value(
             QStringLiteral("runtimeTerrainTypes")).toArray())
        candidate.runtimeTerrainTypes.append(value.toInt());
    for(const QJsonValue& value : manifest.value(
             QStringLiteral("pages")).toArray())
    {
        const QJsonObject pageObject = value.toObject();
        STerrainAtlasRecoveryPage page;
        page.name = pageObject.value(QStringLiteral("name")).toString();
        if(page.name.isEmpty()
           || !readObject(pageObject.value(QStringLiteral("sha256"))
                              .toString().toLatin1(),
                          &page.payload, error))
            return false;
        candidate.pages.append(page);
    }
    for(const QJsonValue& value : manifest.value(
             QStringLiteral("metadataMembers")).toArray())
    {
        const QJsonObject memberObject = value.toObject();
        STerrainAtlasRecoveryPage member;
        member.name = memberObject.value(QStringLiteral("name")).toString();
        if(member.name.isEmpty()
           || !readObject(memberObject.value(QStringLiteral("sha256"))
                              .toString().toLatin1(),
                          &member.payload, error))
            return false;
        candidate.metadataMembers.append(member);
    }
    if(candidate.pages.isEmpty())
    {
        if(error) *error = QStringLiteral(
            "A terrain-atlas recovery manifest contains no atlas pages: %1")
            .arg(manifestPath);
        return false;
    }
    *snapshot = candidate;
    return true;
}
