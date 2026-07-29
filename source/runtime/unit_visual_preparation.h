#ifndef UNIT_VISUAL_PREPARATION_H
#define UNIT_VISUAL_PREPARATION_H

#include "profile_database.h"
#include "runtime_providers.h"

#include <QJsonObject>
#include <QSharedPointer>
#include <QVector>

namespace ei {

inline bool shouldShowUnitAppearanceProgress(bool pending, int total, int completed)
{
    return pending && total > 0 && completed < total;
}

inline int completedUnitVisualCount(int total, int unresolved)
{
    return qBound(0, total - qMax(0, unresolved), qMax(0, total));
}

inline bool shouldPumpUnitVisualPreparation(bool pending, int total, int completed)
{
    return pending && total > 0 && completed < total;
}

inline bool shouldLogUnitVisualProgress(int total, int completed, int lastLoggedCompleted)
{
    // A completed 0/0 job is an idle state, not progress. Treating it as
    // completed caused a synchronous workflow-log write on every viewport
    // frame and every event-pump iteration during MOB loading.
    return total > 0
        && (completed == total
            || lastLoggedCompleted < 0
            || completed - lastLoggedCompleted >= 32);
}

inline bool canStartUnitAppearanceBatch(bool batchPending, bool watcherRunning)
{
    // QFutureWatcher::isRunning() can turn false before its queued finished
    // handler consumes the result. Batch ownership must survive that window.
    return !batchPending && !watcherRunning;
}

inline int unitVisualFinalizationLimit(bool cameraMoving)
{
    // Do not suspend the visible character-resolution job while the user is
    // navigating. One finalization per moving frame keeps progress continuous
    // without applying the larger idle-frame batch to camera interaction.
    return cameraMoving ? 1 : 8;
}

}

struct SUnitAppearancePreparationTask
{
    quintptr unitKey{0};
    quint64 databaseGeneration{0};
    quint64 visualRevision{0};
    QString providerId;
    QSharedPointer<const ei::SProfileDatabaseSnapshot> database;
    SCharacterAppearanceRequest request;
};

struct SUnitAppearancePreparationResult
{
    quintptr unitKey{0};
    quint64 databaseGeneration{0};
    quint64 visualRevision{0};
    SRuntimeProviderResult providerResult;
    QJsonObject appearance;
    QString diagnostic;
    bool cacheHit{false};
};

QString unitAppearancePreparationCacheKey(const SUnitAppearancePreparationTask& task);
QString unitAppearancePreparationCacheDirectoryForApplication(const QString& applicationDirectory);
QString unitAppearancePreparationCacheDirectory();
QVector<SUnitAppearancePreparationResult> resolveUnitAppearanceTasks(
    const QVector<SUnitAppearancePreparationTask>& tasks,
    const QString& cacheDirectory = QString());

#endif
