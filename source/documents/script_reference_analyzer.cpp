#include "script_reference_analyzer.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>

QVector<SMobScriptReference> CScriptReferenceAnalyzer::findObjectIds(
    const QString& script, const QVector<uint>& objectIds)
{
    QVector<SMobScriptReference> references;
    if(script.isEmpty() || objectIds.isEmpty())
        return references;

    QSet<uint> uniqueIds;
    for(uint id : objectIds)
        uniqueIds.insert(id);

    const QStringList lines = script.split(QRegularExpression("\\r?\\n"));
    QList<uint> sortedIds = uniqueIds.values();
    std::sort(sortedIds.begin(), sortedIds.end());
    for(uint id : sortedIds)
    {
        const QString idText = QString::number(id);
        const QRegularExpression expression(
            QString("(?<![0-9])%1(?![0-9])").arg(QRegularExpression::escape(idText)));
        for(int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            QRegularExpressionMatchIterator matches = expression.globalMatch(lines.at(lineIndex));
            while(matches.hasNext())
            {
                const QRegularExpressionMatch match = matches.next();
                SMobScriptReference reference;
                reference.objectId = id;
                reference.line = lineIndex + 1;
                reference.column = match.capturedStart() + 1;
                reference.context = lines.at(lineIndex).trimmed();
                if(reference.context.size() > 180)
                    reference.context = reference.context.left(177) + "...";
                references.append(reference);
            }
        }
    }
    return references;
}
