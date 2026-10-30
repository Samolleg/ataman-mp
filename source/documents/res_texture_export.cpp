#include "res_texture_export.h"
#include "res_save_coordinator.h"
#include "res_archive_index.h"
#include "process_edit_lock.h"
#include "file_write_activity.h"
#include "mmp_inspector.h"
#include <QDataStream>

namespace {
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
    CFileWriteActivity writing;
    SResTextureExportResult result;
    result.phase = "input";
    QString memberName = request.memberName.trimmed();
    if(!memberName.endsWith(".mmp", Qt::CaseInsensitive)) memberName += ".mmp";
    const auto mmp = ei::inspectMmp(request.mmpPayload);
    if(request.destination.absoluteFilePath().isEmpty() || memberName == ".mmp"
       || !mmp.valid || mmp.width != mmp.height || mmp.width < 64
       || mmp.width > 2048 || (mmp.width & (mmp.width - 1)) != 0)
    {
        result.error = mmp.valid
            ? "Export requires a destination, texture name, and a square power-of-two MMP from 64 to 2048 pixels."
            : "MMP validation failed: " + mmp.diagnostic;
        return result;
    }
    const QString path = request.destination.absoluteFilePath();
    auto lease = CProcessEditLock::acquire(path, &result.error, false);
    if(!lease) return result;
    const bool existed = QFileInfo::exists(path);
    if(existed)
    {
        CResArchiveIndex source;
        if(!source.open(path, &result.error)) return result;
        const int index = source.memberIndex(memberName);
        if(index == -2) { result.error = "Ambiguous RES texture member: " + memberName; return result; }
        if(index >= 0 && request.conflictPolicy != EResTextureConflictPolicy::Replace)
        { result.error = "RES member already exists; Replace, Rename, or Cancel is required: " + memberName; return result; }
        result.replacedMember = index >= 0;
        result.oldArchiveSha256 = resFileSha256(path, &result.error);
        if(result.oldArchiveSha256.isEmpty()) return result;
#ifdef QT_TESTLIB_LIB
        if(request.simulateValidationFailure) { result.error = "Simulated staged validation failure."; return result; }
#endif
        SResPublishedMembersSaveRequest save;
        save.source = save.destination = request.destination;
        SResArchiveNewMember member;
        member.name = memberName;
        member.payload = request.mmpPayload;
        save.published.append(member);
        save.allowReplaceExisting = request.conflictPolicy == EResTextureConflictPolicy::Replace;
        save.backupRootOverride = request.backupRootOverride;
#ifdef QT_TESTLIB_LIB
        save.simulateAtomicCommitFailure = request.simulateAtomicCommitFailure;
        save.beforeCommitForTest = request.beforeCommitForTest;
#endif
        const auto committed = commitStreamedResMembers(source, save, {}, result.oldArchiveSha256);
        result.success = committed.success;
        result.phase = committed.phase;
        result.error = committed.error;
        result.backupPath = committed.backupPath;
        result.validationNotes = committed.validationNotes;
        result.newArchiveSha256 = committed.noOp ? result.oldArchiveSha256
                                               : committed.candidateSha256.toLatin1();
    }
    else
    {
        // Only a new one-texture archive is memory-backed. Existing payloads
        // always stay in the file-backed transaction above.
        CResArchiveDocument seed;
        QByteArray candidate;
        if(!seed.loadBytes(emptyArchive(), path, &result.validationNotes)
           || !seed.buildWithAppendedMember(memberName, request.mmpPayload, 0,
                                            &candidate, &result.validationNotes))
        { result.error = result.validationNotes.join('\n'); return result; }
#ifdef QT_TESTLIB_LIB
        if(request.simulateValidationFailure) { result.error = "Simulated staged validation failure."; return result; }
#endif
        SResArchiveBytesSaveRequest save;
        save.destination = request.destination;
        save.candidateBytes = candidate;
        save.requireAbsent = true;
        save.backupRootOverride = request.backupRootOverride;
#ifdef QT_TESTLIB_LIB
        save.simulateAtomicCommitFailure = request.simulateAtomicCommitFailure;
        save.beforeCommitForTest = request.beforeCommitForTest;
#endif
        const auto committed = commitValidatedResArchiveBytes(save);
        result.success = committed.success;
        result.phase = committed.phase;
        result.error = committed.error;
        result.backupPath = committed.backupPath;
        result.newArchiveSha256 = committed.candidateSha256.toLatin1();
    }
    result.createdArchive = result.success && !existed;
    return result;
}
