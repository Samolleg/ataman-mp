#ifndef SCRIPT_REFERENCE_ANALYZER_H
#define SCRIPT_REFERENCE_ANALYZER_H

#include <QString>
#include <QVector>

struct SMobScriptReference
{
    uint objectId{0};
    int line{0};
    int column{0};
    QString context;
};

class CScriptReferenceAnalyzer
{
public:
    // Conservative by design: an exact standalone decimal token counts as a
    // reference whether it appears in a helper call, a string, or a comment.
    // The result is warning-only; Endurance never rewrites script text here.
    static QVector<SMobScriptReference> findObjectIds(const QString& script,
                                                      const QVector<uint>& objectIds);
};

#endif // SCRIPT_REFERENCE_ANALYZER_H
