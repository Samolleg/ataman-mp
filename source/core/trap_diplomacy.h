#ifndef TRAP_DIPLOMACY_H
#define TRAP_DIPLOMACY_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace ei {

struct STrapTargetSummary
{
    bool available = false;
    int playerGroup = -1;
    quint32 mask = 0;
    QVector<int> groups;
    QStringList labels;
    QString display;
};

STrapTargetSummary deriveTrapTargetSummary(
    int playerGroup,
    const QVector<QVector<uint>>& diplomacy,
    const QStringList& playerNames,
    quint32 storedMask = 0);

}

#endif
