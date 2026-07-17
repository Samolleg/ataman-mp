#include "map_catalog.h"

#include <QRegularExpression>

QVector<SMapCatalogEntry> CMapCatalog::parseMapTable(const QString& text,
                                                     const QString& sourceArchive)
{
    QVector<SMapCatalogEntry> entries;
    SMapCatalogEntry current;
    enum class PendingValue { None, Resources, ExitTarget, Deploy, View };
    PendingValue pending = PendingValue::None;
    int currentExitIndex = -1;
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
            pending = PendingValue::None;
            currentExitIndex = -1;
            continue;
        }
        if(line.compare("#res", Qt::CaseInsensitive) == 0)
        {
            pending = current.zoneId.isEmpty() ? PendingValue::None : PendingValue::Resources;
            continue;
        }
        if(line.startsWith("#exit", Qt::CaseInsensitive))
        {
            const QStringList parts = line.split(whitespace, QString::SkipEmptyParts);
            bool validExit = false;
            const int exitId = parts.size() >= 2 ? parts.at(1).toInt(&validExit) : 0;
            if(!current.zoneId.isEmpty() && validExit)
            {
                SMapTravelZone travelZone;
                travelZone.exitId = exitId;
                current.travelZones.append(travelZone);
                currentExitIndex = current.travelZones.size() - 1;
                pending = PendingValue::ExitTarget;
            }
            else
            {
                currentExitIndex = -1;
                pending = PendingValue::None;
            }
            continue;
        }
        if(line.compare("#deploy", Qt::CaseInsensitive) == 0)
        {
            pending = currentExitIndex >= 0 ? PendingValue::Deploy : PendingValue::None;
            continue;
        }
        if(line.compare("#view", Qt::CaseInsensitive) == 0)
        {
            pending = currentExitIndex >= 0 ? PendingValue::View : PendingValue::None;
            continue;
        }
        if(line.startsWith('#'))
        {
            pending = PendingValue::None;
            continue;
        }
        const QStringList parts = line.split(whitespace, QString::SkipEmptyParts);
        if(pending == PendingValue::Resources)
        {
            if(parts.size() >= 2)
            {
                current.mprName = parts.at(0);
                current.mobName = parts.at(1);
            }
        }
        else if(pending == PendingValue::ExitTarget && currentExitIndex >= 0)
        {
            if(parts.size() >= 2)
            {
                current.travelZones[currentExitIndex].destinationZoneId = parts.at(0);
                bool validDestinationExit = false;
                const int destinationExit = parts.at(1).toInt(&validDestinationExit);
                if(validDestinationExit)
                    current.travelZones[currentExitIndex].destinationExitId = destinationExit;
            }
        }
        else if(pending == PendingValue::Deploy && currentExitIndex >= 0)
        {
            if(parts.size() >= 4)
            {
                bool valid[4] = {false, false, false, false};
                const double x1 = parts.at(0).toDouble(&valid[0]);
                const double y1 = parts.at(1).toDouble(&valid[1]);
                const double x2 = parts.at(2).toDouble(&valid[2]);
                const double y2 = parts.at(3).toDouble(&valid[3]);
                if(valid[0] && valid[1] && valid[2] && valid[3])
                {
                    SMapTravelZone& travelZone = current.travelZones[currentExitIndex];
                    travelZone.deployRectangle = QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized();
                    travelZone.hasDeployRectangle = true;
                }
            }
        }
        else if(pending == PendingValue::View && currentExitIndex >= 0 && !parts.isEmpty())
        {
            bool validView = false;
            const double view = parts.at(0).toDouble(&validView);
            if(validView)
            {
                current.travelZones[currentExitIndex].viewAngle = view;
                current.travelZones[currentExitIndex].hasViewAngle = true;
            }
        }
        pending = PendingValue::None;
    }
    finishCurrent();
    return entries;
}
