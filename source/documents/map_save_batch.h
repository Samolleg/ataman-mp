#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

struct SMapSaveFile
{
    QString path;
    QByteArray before;
    QByteArray after;
    bool existed{true};
};
struct SMapSaveBatchResult
{
    bool success{false};
    bool rollbackComplete{true};
    QString error;
    QStringList backups;
};
// Every candidate must already have passed its format-specific validation.
// Stage and back up the whole set before replacing anything. Recoverable
// failures restore committed files only if they still belong to this save.
SMapSaveBatchResult commitMapSaveBatch(const QVector<SMapSaveFile>& files,
                                     const QString& backupRoot = QString(),
                                     int failCommitForTest = -1);
