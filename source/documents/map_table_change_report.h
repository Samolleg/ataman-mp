#ifndef MAP_TABLE_CHANGE_REPORT_H
#define MAP_TABLE_CHANGE_REPORT_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

struct SMapTableSemanticChange { QString zoneId; QStringList categories; };

struct SMapTableChangeAnalysis
{
    bool valid{false};
    QString error;
    QVector<SMapTableSemanticChange> zones;
    QStringList categories;
    QStringList affectedZoneIds() const;
    bool changesPhysicalResources(const QString& zoneId) const;
};

struct SMapTableSaveReport
{
    QString destinationArchive;
    QByteArray oldArchiveSha256;
    QByteArray newArchiveSha256;
    QByteArray oldMapTableSha256;
    QByteArray newMapTableSha256;
    SMapTableChangeAnalysis changes;
};

SMapTableChangeAnalysis analyzeMapTableChange(const QByteArray& before,
                                              const QByteArray& after);

#endif
