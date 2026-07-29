#ifndef UNIT_VISUAL_PREPARATION_H
#define UNIT_VISUAL_PREPARATION_H

#include "profile_database.h"
#include "runtime_providers.h"

#include <QJsonObject>
#include <QSharedPointer>
#include <QVector>

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
};

QVector<SUnitAppearancePreparationResult> resolveUnitAppearanceTasks(
    const QVector<SUnitAppearancePreparationTask>& tasks);

#endif
