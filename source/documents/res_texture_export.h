#ifndef RES_TEXTURE_EXPORT_H
#define RES_TEXTURE_EXPORT_H

#include <QByteArray>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <functional>

enum class EResTextureConflictPolicy { Refuse, Replace };

struct SResTextureExportRequest
{
    QFileInfo destination;
    QString memberName;
    QByteArray mmpPayload;
    EResTextureConflictPolicy conflictPolicy{EResTextureConflictPolicy::Refuse};
    QString backupRootOverride;
#ifdef QT_TESTLIB_LIB
    bool simulateValidationFailure{false};
    bool simulateAtomicCommitFailure{false};
    std::function<void()> beforeCommitForTest;
#endif
};

struct SResTextureExportResult
{
    bool success{false};
    bool createdArchive{false};
    bool replacedMember{false};
    QString phase;
    QString error;
    QString backupPath;
    QByteArray oldArchiveSha256;
    QByteArray newArchiveSha256;
    QStringList validationNotes;
};

SResTextureExportResult exportMmpToRes(const SResTextureExportRequest& request);

#endif
