#ifndef RESOURCE_CATALOG_H
#define RESOURCE_CATALOG_H

#include "res_archive_index.h"

#include <QStringList>
#include <QVector>

struct SResourceCatalogEntry
{
    QString logicalName;
    QString archivePath;
    QString archiveName;
    int archivePriority{-1};
    int memberIndex{-1};
    quint32 size{0};
    quint32 lastWriteTime{0};
    bool winning{false};
};

class CResourceCatalog
{
public:
    bool build(const QVector<QString>& archivePaths, QStringList* errors = nullptr);
    const QVector<SResourceCatalogEntry>& entries() const { return m_entries; }
    QVector<SResourceCatalogEntry> providers(const QString& logicalName) const;
    QVector<SResourceCatalogEntry> entriesForArchive(const QString& archivePath) const;
    const CResArchiveIndex* archive(int priority) const;

private:
    QVector<CResArchiveIndex> m_archives;
    QVector<SResourceCatalogEntry> m_entries;
};

#endif // RESOURCE_CATALOG_H
