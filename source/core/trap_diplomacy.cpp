#include "trap_diplomacy.h"

namespace ei {

STrapTargetSummary deriveTrapTargetSummary(
    int playerGroup,
    const QVector<QVector<uint>>& diplomacy,
    const QStringList& playerNames,
    quint32 storedMask)
{
    Q_UNUSED(storedMask);
    STrapTargetSummary result;
    if(playerGroup < 0 || playerGroup >= 32)
    {
        result.display = QString("Unavailable — invalid Player group %1")
            .arg(playerGroup);
        return result;
    }
    if(diplomacy.size() <= playerGroup || diplomacy.at(playerGroup).size() < 32)
    {
        result.display = QString(
            "Unavailable — no complete diplomacy row for Player group %1")
            .arg(playerGroup);
        return result;
    }

    result.available = true;
    QStringList targets;
    const QVector<uint>& row = diplomacy.at(playerGroup);
    for(int target = 0; target < 32; ++target)
    {
        // MOB diplomacy values are 0 Ally, 1 Neutral, 2 War. Runtime creates
        // the trap mask from War entries only and replaces MT_DIPLOMACY.
        if(row.at(target) != 2)
            continue;
        result.mask |= quint32(1u << target);
        result.groups.append(target);
        const QString name = target < playerNames.size()
            ? playerNames.at(target).trimmed() : QString();
        targets.append(name.isEmpty()
            ? QString::number(target)
            : QString("%1 (%2)").arg(target).arg(name));
    }

    const QString targetText = targets.isEmpty()
        ? QString("none (no groups marked War)")
        : targets.join(", ");
    result.display = QString("Player group %1 → %2")
        .arg(playerGroup).arg(targetText);
    return result;
}

}
