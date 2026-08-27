#include "resource_catalog.h"

#include <QFileInfo>
#include <QDir>
#include <QSet>

bool CResourceCatalog::build(const QVector<QString>& archivePaths, QStringList* errors)
{
    m_archives.clear();
    m_entries.clear();
    if(errors) errors->clear();
    QSet<QString> winners;
    for(int priority = 0; priority < archivePaths.size(); ++priority)
    {
        CResArchiveIndex index;
        QString error;
        if(!index.open(archivePaths.at(priority), &error))
        {
            if(errors) errors->append(QString("%1: %2").arg(archivePaths.at(priority), error));
            continue;
        }
        const int storedPriority = m_archives.size();
        m_archives.append(index);
        for(int memberIndex = 0; memberIndex < index.memberCount(); ++memberIndex)
        {
            const SResIndexedMember& member = index.member(memberIndex);
            const QString key = member.name.toCaseFolded();
            SResourceCatalogEntry entry;
            entry.logicalName = member.name;
            entry.archivePath = index.path();
            entry.archiveName = QFileInfo(index.path()).fileName();
            entry.archivePriority = storedPriority;
            entry.memberIndex = memberIndex;
            entry.size = member.dataSize;
            entry.lastWriteTime = member.lastWriteTime;
            entry.winning = !winners.contains(key);
            winners.insert(key);
            m_entries.append(entry);
        }
    }
    return !m_archives.isEmpty();
}

QVector<SResourceCatalogEntry> CResourceCatalog::providers(const QString& logicalName) const
{
    QVector<SResourceCatalogEntry> result;
    for(const SResourceCatalogEntry& entry : m_entries)
        if(entry.logicalName.compare(logicalName, Qt::CaseInsensitive) == 0)
            result.append(entry);
    return result;
}

QVector<SResourceCatalogEntry> CResourceCatalog::entriesForArchive(const QString& archivePath) const
{
    QVector<SResourceCatalogEntry> result;
    const QString requested = QDir::cleanPath(QFileInfo(archivePath).absoluteFilePath());
    for(const SResourceCatalogEntry& entry : m_entries)
        if(QDir::cleanPath(QFileInfo(entry.archivePath).absoluteFilePath())
               .compare(requested, Qt::CaseInsensitive) == 0)
            result.append(entry);
    return result;
}

const CResArchiveIndex* CResourceCatalog::archive(int priority) const
{
    return priority >= 0 && priority < m_archives.size() ? &m_archives.at(priority) : nullptr;
}
