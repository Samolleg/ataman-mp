#ifndef SCENARIO_ANALYSIS_H
#define SCENARIO_ANALYSIS_H

#include <QStringList>
#include <QVector>
#include <QVector3D>
#include <QSet>

// Value-only static analysis. Never receives a document, script interpreter,
// mutable CNode, undo stack or resource writer.
namespace scenario {
enum class Resolution { Local, Family, Virtual, External, Ambiguous, Missing,
                        Dynamic, Invalid, Incomplete };
enum class Kind { Lookup, Spawn, Move, Control, Group, Dependency, Other };
struct Object {
    QString key;
    quint32 id = 0;
    QString name;
    QString category;
    QVector3D position;
    bool hasPosition = false;
};
struct Source {
    QString key;
    QString text;
    QVector<Object> objects;
    bool draft = false;
};
struct Request {
    QVector<Source> sources;
    QString activeSource;
    QStringList familyRoots;
    QSet<QString> prototypes;
};
struct Target {
    QString expression;
    Resolution resolution = Resolution::Dynamic;
    QString sourceKey;
    QString objectKey;
    QString name;
    quint32 id = 0;
    bool hasId = false;
};
struct Reference {
    QString sourceKey;
    QString function;
    QString command;
    QString context;
    QStringList conditions;
    QString status;
    Kind kind = Kind::Other;
    int offset = 0;
    int length = 0;
    int line = 1;
    int column = 1;
    QVector<Target> targets;
    QString spawnName;
    QString prototype;
    bool prototypeVerified = false;
    bool hasDestination = false;
    bool destinationHasZ = false;
    QVector3D destination;
    bool complete = true;
};
struct Marker {
    QString sourceKey;
    QString participantKey;
    int reference = -1;
    bool spawn = false;
    bool hasZ = false;
    QVector3D position;
};
struct Result {
    QVector<Reference> references;
    QVector<Marker> markers;
    QSet<QString> family;
    QStringList diagnostics;
    bool familyComplete = true;
};
QString normalizedSource(const QString& key);
QString normalizedText(QString text);
QString resolutionLabel(Resolution value);
QString kindLabel(Kind value);
bool navigable(Resolution value);
Result analyze(const Request& request);
}
#endif
