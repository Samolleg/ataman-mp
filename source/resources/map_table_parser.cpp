#include "map_catalog.h"

#include <QRegularExpression>

QVector<SMapCatalogEntry> CMapCatalog::parseMapTable(const QString& text,
                                                     const QString& sourceArchive)
{
    QVector<SMapCatalogEntry> entries;
    SMapCatalogEntry current;
    bool waitingForResources = false;
    const QStringList lines = text.split(QRegularExpression("\\r?\\n"));
    const QRegularExpression whitespace("\\s+");

    const auto finishCurrent = [&entries, &current]()
    {
        if(!current.zoneId.isEmpty() && !current.mprName.isEmpty() && !current.mobName.isEmpty())
            entries.append(current);
        current = SMapCatalogEntry();
    };

    for(QString line : lines)
    {
        line = line.trimmed();
        if(line.isEmpty() || line.startsWith("##"))
            continue;
        if(line.startsWith("#zone", Qt::CaseInsensitive))
        {
            finishCurrent();
            const QStringList parts = line.split(whitespace, QString::SkipEmptyParts);
            if(parts.size() >= 2)
                current.zoneId = parts.at(1);
            if(parts.size() >= 3)
                current.allodId = parts.at(2);
            if(parts.size() >= 4)
                current.zoneType = parts.at(3);
            current.sourceArchive = sourceArchive;
            waitingForResources = false;
            continue;
        }
        if(line.compare("#res", Qt::CaseInsensitive) == 0)
        {
            waitingForResources = !current.zoneId.isEmpty();
            continue;
        }
        if(line.startsWith('#'))
        {
            waitingForResources = false;
            continue;
        }
        if(waitingForResources)
        {
            const QStringList parts = line.split(whitespace, QString::SkipEmptyParts);
            if(parts.size() >= 2)
            {
                current.mprName = parts.at(0);
                current.mobName = parts.at(1);
            }
            waitingForResources = false;
        }
    }
    finishCurrent();
    return entries;
}
