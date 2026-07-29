#include "unit_visual_preparation.h"
#include "profile_visual_provider.h"

QVector<SUnitAppearancePreparationResult> resolveUnitAppearanceTasks(
    const QVector<SUnitAppearancePreparationTask>& tasks)
{
    QVector<SUnitAppearancePreparationResult> results;
    results.reserve(tasks.size());
    ProfileVisualProviderService providers;
    for(const SUnitAppearancePreparationTask& task : tasks)
    {
        SUnitAppearancePreparationResult result;
        result.unitKey = task.unitKey;
        result.databaseGeneration = task.databaseGeneration;
        result.visualRevision = task.visualRevision;
        const auto resolver = providers.appearanceResolver(
            task.providerId, task.database, &result.diagnostic);
        if(resolver.isNull())
        {
            result.providerResult.code = QStringLiteral("provider.unavailable");
            result.providerResult.message = result.diagnostic;
        }
        else
            result.providerResult = resolver->resolve(task.request, &result.appearance);
        results.append(result);
    }
    return results;
}
