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
    // Only literal object lookups are references: GetObject(number) and
    // GetObjectByID("number"). Comments, quoted prose, coordinates, flags, and
    // unrelated numeric arguments are excluded. Endurance never rewrites script
    // text here; dynamic lookup expressions remain a manual script-review case.
    static QVector<SMobScriptReference> findObjectIds(const QString& script);
    static QVector<SMobScriptReference> findObjectIds(const QString& script,
                                                      const QVector<uint>& objectIds);
};

#endif // SCRIPT_REFERENCE_ANALYZER_H
