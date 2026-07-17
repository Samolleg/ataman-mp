#include "duplicate_id_report.h"

#include <QHash>
#include <QStringList>

QString ei::duplicateIdWarningText(const QVector<uint>& duplicateOccurrences,
                                   int previewLimit)
{
    if(duplicateOccurrences.isEmpty())
        return QString();

    QHash<uint, int> counts;
    QVector<uint> idsInEncounterOrder;
    for(uint id : duplicateOccurrences)
    {
        if(!counts.contains(id))
            idsInEncounterOrder.append(id);
        counts[id] += 1;
    }

    previewLimit = qMax(1, previewLimit);
    const int shownCount = qMin(previewLimit, idsInEncounterOrder.size());
    QStringList preview;
    for(int i = 0; i < shownCount; ++i)
    {
        const uint id = idsInEncounterOrder.at(i);
        const int count = counts.value(id);
        preview.append(QString("- ID %1: %2 duplicate occurrence%3")
            .arg(id)
            .arg(count)
            .arg(count == 1 ? QString() : QString("s")));
    }

    QString text = QString(
        "Duplicate object IDs were found. Endurance Edition will preserve them during editing and saving, "
        "but duplicate IDs can cause unpredictable behavior in Evil Islands.\n\n"
        "Duplicate IDs (showing %1 of %2):\n%3")
        .arg(shownCount)
        .arg(idsInEncounterOrder.size())
        .arg(preview.join("\n"));

    const int omittedCount = idsInEncounterOrder.size() - shownCount;
    if(omittedCount > 0)
        text += QString("\n- ...and %1 more duplicate ID%2")
            .arg(omittedCount)
            .arg(omittedCount == 1 ? QString() : QString("s"));

    text += "\n\nFull duplicate details were written to logs\\workflow.log.";
    return text;
}
