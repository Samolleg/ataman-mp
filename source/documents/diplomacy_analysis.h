#pragma once
#include "scenario_analysis.h"
#include <QMap>

namespace diplomacy {
struct Npc {
    QString key, name, prototype;
    quint32 id = 0;
    bool hasId = true;
    int group = -1; // Missing/unknown, never silently assigned to player zero.
    bool scripted = false;
    int line = 0;
    QSet<int> possibleGroups;
};
struct Source {
    QString key, script;
    QVector<Npc> npcs;
    QVector<scenario::Object> objects; // Also reserve identities of non-NPC objects.
    QStringList groupNames;
    QVector<QVector<quint32>> relations;
    bool loaded = false, draft = false;
    QString reason;
};
struct Request {
    QVector<Source> sources;
    QString preferredSource;
    QStringList roots, extraSources;
};
struct Event {
    scenario::Reference reference;
    int group = -1;
    QString npcKey, npcSource;
};
struct Result {
    QVector<Source> sources;
    QVector<Event> events;
    QStringList diagnostics;
};
// Pure inspection: literal calls describe possible execution, not runtime state.
Result analyze(const QVector<Source>& sources);
// Worker-only file inspection. Live sources are authoritative value snapshots;
// no CNode pointers, resource scans, script execution or writes are allowed.
Result inspectConnected(const Request& request);
}
