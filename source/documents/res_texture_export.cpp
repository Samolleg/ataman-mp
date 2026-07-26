#include "res_texture_export.h"

#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QHash>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QDataStream>

#include "backup_manager.h"
#include "mmp_inspector.h"
#include "res_archive_document.h"

namespace {
QByteArray sha(const QByteArray& bytes)
{ return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(); }

bool readAll(const QString& path, QByteArray* bytes, QString* error)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    { if(error) *error = file.errorString(); return false; }
    *bytes = file.readAll();
    if(file.error() != QFileDevice::NoError)
    { if(error) *error = file.errorString(); return false; }
    return true;
}

QByteArray emptyArchive()
{
    QByteArray bytes(16, '\0');
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x019ce23c) << quint32(0) << quint32(16) << quint32(0);
    return bytes;
}
}

SResTextureExportResult exportMmpToRes(const SResTextureExportRequest& request)
{
    SResTextureExportResult result;
    result.phase = "input";
    QString memberName = request.memberName.trimmed();
    if(!memberName.endsWith(".mmp", Qt::CaseInsensitive)) memberName += ".mmp";
    const ei::SMmpInspection mmp = ei::inspectMmp(request.mmpPayload);
    if(request.destination.absoluteFilePath().isEmpty() || memberName == ".mmp"
       || !mmp.valid || mmp.width != 256 || mmp.height != 256)
    {
        result.error = mmp.valid
            ? "Export requires a destination, texture name, and 256x256 MMP."
            : "MMP validation failed: " + mmp.diagnostic;
        return result;
    }

    const QString path = request.destination.absoluteFilePath();
    const bool existed = QFileInfo::exists(path);
    QByteArray original = emptyArchive();
    if(existed && !readAll(path, &original, &result.error)) return result;
    result.oldArchiveSha256 = existed ? sha(original) : QByteArray();

    CResArchiveDocument baseline;
    if(!baseline.loadBytes(original, path, &result.validationNotes))
    { result.error = result.validationNotes.join('\n'); return result; }
    int matchingMembers = 0;
    QByteArray current;
    for(const SResArchiveMember& member : baseline.members())
    {
        if(member.name.compare(memberName, Qt::CaseInsensitive) == 0)
        {
            ++matchingMembers;
            current = member.payload;
        }
    }
    if(matchingMembers > 1)
    {
        result.error = QString("RES contains ambiguous duplicate members named %1.")
            .arg(memberName);
        return result;
    }
    const bool memberExists = matchingMembers == 1;
    QByteArray candidate;
    result.phase = "serialization";
    if(memberExists)
    {
        if(request.conflictPolicy != EResTextureConflictPolicy::Replace)
        { result.error = QString("RES member %1 already exists; Replace, Rename, or Cancel is required.").arg(memberName); return result; }
        if(!baseline.replaceMember(memberName, sha(current), request.mmpPayload,
                                   &candidate, &result.validationNotes))
        { result.error = result.validationNotes.join('\n'); return result; }
        result.replacedMember = true;
    }
    else if(!baseline.buildWithAppendedMember(memberName, request.mmpPayload, 0,
                                               &candidate, &result.validationNotes))
    { result.error = result.validationNotes.join('\n'); return result; }

    result.phase = "staged-reopen";
    QTemporaryFile stage(QFileInfo(path).absoluteDir().filePath(
        ".EIMapperEndurance-texture-stage-XXXXXX.res"));
    stage.setAutoRemove(true);
    if(!stage.open() || stage.write(candidate) != candidate.size() || !stage.flush())
    { result.error = stage.errorString(); return result; }
    const QString stagePath = stage.fileName();
    stage.close();
    QByteArray reopenedBytes;
    CResArchiveDocument reopened;
    if(!readAll(stagePath, &reopenedBytes, &result.error)
       || reopenedBytes != candidate
       || !reopened.loadBytes(reopenedBytes, stagePath, &result.validationNotes)
       || reopened.memberPayload(memberName) != request.mmpPayload)
    { if(result.error.isEmpty()) result.error = "Fresh RES reopen validation failed."; return result; }
    const ei::SMmpInspection reopenedMmp = ei::inspectMmp(reopened.memberPayload(memberName));
    if(!reopenedMmp.valid || reopenedMmp.sha256 != mmp.sha256)
    { result.error = "Freshly reopened RES contains a different or invalid MMP."; return result; }
    const int expectedCount = baseline.members().size() + (memberExists ? 0 : 1);
    if(reopened.members().size() != expectedCount)
    { result.error = "RES member count changed unexpectedly."; return result; }
    QHash<QString, const SResArchiveMember*> reopenedByName;
    for(const SResArchiveMember& member : reopened.members())
    {
        const QString key = member.name.toCaseFolded();
        if(reopenedByName.contains(key))
        {
            result.error = QString("Fresh RES reopen contains ambiguous duplicate member: %1")
                .arg(member.name);
            return result;
        }
        reopenedByName.insert(key, &member);
    }
    for(const SResArchiveMember& before : baseline.members())
    {
        const SResArchiveMember* after = reopenedByName.value(
            before.name.toCaseFolded(), nullptr);
        if(!after)
        {
            result.error = QString("Fresh RES reopen lost member: %1").arg(before.name);
            return result;
        }
        if(before.encodedName != after->encodedName
           || before.lastWriteTime != after->lastWriteTime
           || (before.name.compare(memberName, Qt::CaseInsensitive) != 0
               && before.payload != after->payload))
        { result.error = QString("Untouched RES member changed: %1").arg(before.name); return result; }
    }
#ifdef QT_TESTLIB_LIB
    if(request.simulateValidationFailure)
    { result.error = "Simulated staged validation failure."; return result; }
#endif

    QByteArray fresh;
    if(existed && (!readAll(path, &fresh, &result.error) || fresh != original))
    { if(result.error.isEmpty()) result.error = "Destination changed during staging."; return result; }
    if(existed)
    {
        result.phase = "backup";
        const SBackupResult backup = CBackupManager(
            request.backupRootOverride).snapshotBeforeSave(QFileInfo(path));
        if(!backup.success || backup.sha256.compare(QString::fromLatin1(sha(original)),
                                                   Qt::CaseInsensitive) != 0)
        { result.error = backup.success ? "Backup hash mismatch." : backup.error; return result; }
        result.backupPath = backup.backupPath;
    }
    result.phase = "atomic-publish";
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly) || output.write(candidate) != candidate.size()
       || !output.commit())
    { result.error = output.errorString(); return result; }
    QByteArray published;
    if(!readAll(path, &published, &result.error) || published != candidate)
    { if(result.error.isEmpty()) result.error = "Published RES hash mismatch."; return result; }
    CResArchiveDocument publishedArchive;
    QStringList publishedNotes;
    if(!publishedArchive.loadBytes(published, path, &publishedNotes)
       || publishedArchive.memberPayload(memberName) != request.mmpPayload)
    {
        result.validationNotes.append(publishedNotes);
        result.error = "Published RES failed its fresh structural reopen validation.";
        return result;
    }
    const ei::SMmpInspection publishedMmp =
        ei::inspectMmp(publishedArchive.memberPayload(memberName));
    if(!publishedMmp.valid || publishedMmp.sha256 != mmp.sha256)
    {
        result.error = "Published RES contains a different or invalid MMP.";
        return result;
    }
    result.success = true;
    result.createdArchive = !existed;
    result.newArchiveSha256 = sha(candidate);
    result.phase = "complete";
    return result;
}
