#include "character_bodypart_runtime.h"
#include <QSet>

bool ei::standardWearableAssembly(const QString& model)
{
    static const QSet<QString> models = {
        "unhuma", "unhufe", "unorma", "unorfe", "unhusk", "unhuzm", "unhuzf"};
    return models.contains(model.trimmed().toLower());
}

QStringList ei::freshDatabaseBodyParts(const QString& model, int hairIndex)
{
    if(!standardWearableAssembly(model))
        return {};
    // These explicit anatomy roots are the shipped wearable mask, not the
    // dangerous empty-mask meaning of every optional assembly member.
    QStringList parts = {"hp", "bd", "hd", "lh1", "rh1", "lh2", "rh2", "lh3", "rh3",
                         "ll1", "rl1", "ll2", "rl2", "ll3", "rl3"};
    if(legacyHairRootAlias(model))
        parts.insert(3, "hr");
    else if(hairIndex >= 0)
        parts.insert(3, databaseHairPart(hairIndex));
    return parts;
}

namespace {
bool containsPart(const QStringList& parts, const QString& wanted)
{
    return parts.contains(wanted, Qt::CaseInsensitive);
}
}

QString ei::databaseHairPart(int hairIndex)
{
    return hairIndex < 0 ? QString()
        : QString("hr.%1").arg(hairIndex, 2, 10, QChar('0'));
}

bool ei::legacyHairRootAlias(const QString& model)
{
    const QString normalized = model.trimmed().toLower();
    return normalized == "unhuma" || normalized == "unhufe";
}

QStringList ei::serializedAssemblyHairParts(const QStringList& serializedParts,
                                            const QStringList& assemblyParts)
{
    QStringList result;
    const auto appendHair = [&result](const QString& part)
    {
        if((part.compare("hr", Qt::CaseInsensitive) == 0
            || part.startsWith("hr.", Qt::CaseInsensitive))
           && !result.contains(part, Qt::CaseInsensitive))
            result.append(part);
    };
    if(serializedParts.isEmpty())
    {
        for(const QString& part : assemblyParts)
            appendHair(part);
        return result;
    }
    for(const QString& selected : serializedParts)
        for(const QString& part : assemblyParts)
            if(part.compare(selected, Qt::CaseInsensitive) == 0)
            {
                appendHair(part);
                break;
            }
    return result;
}

bool ei::serializedPartsLoadDatabaseHair(const QString& model,
                                         const QStringList& serializedParts,
                                         const QStringList& assemblyParts,
                                         int hairIndex)
{
    const QString nativeHair = databaseHairPart(hairIndex);
    if(nativeHair.isEmpty() || !containsPart(assemblyParts, nativeHair))
        return false;

    // A missing/empty OBJBODYPARTS mask enables the complete assembly. The
    // validator reports that whole-mask state separately; for rendering, the
    // database-selected hair is therefore already loaded before equipment.
    if(serializedParts.isEmpty())
        return true;

    // The client translates the plain root only for these two legacy human
    // templates. Other wearable assemblies load the serialized token
    // literally, so an orc's plain "hr" does not select its database hair.
    if(legacyHairRootAlias(model) && containsPart(serializedParts, "hr"))
        return true;

    for(const QString& selected : serializedParts)
    {
        if(!containsPart(assemblyParts, selected))
            continue;
        if(selected.compare(nativeHair, Qt::CaseInsensitive) == 0
           || selected.startsWith(nativeHair + '.', Qt::CaseInsensitive))
            return true;
    }
    return false;
}

ei::SHelmetHairRuntimeDecision ei::helmetHairRuntimeDecision(
    const QString& model,
    const QStringList& serializedParts,
    const QStringList& assemblyParts,
    int hairIndex,
    int helmetGeometry)
{
    SHelmetHairRuntimeDecision result;
    result.nativeHairPart = databaseHairPart(hairIndex);
    if(result.nativeHairPart.isEmpty() || helmetGeometry < 0)
        return result;

    const QString suffix = QString(".armor%1").arg(
        helmetGeometry, 2, 10, QChar('0'));
    result.helmetPart = "hd" + suffix;
    result.replacementPart = result.nativeHairPart + suffix;
    result.requiredSerializedPart = legacyHairRootAlias(model)
        ? QString("hr") : result.nativeHairPart;
    result.nativeHairAvailable = containsPart(assemblyParts, result.nativeHairPart);
    result.helmetPartAvailable = containsPart(assemblyParts, result.helmetPart);
    result.replacementAvailable = containsPart(assemblyParts, result.replacementPart);
    result.nativeHairInitiallyLoaded = serializedPartsLoadDatabaseHair(
        model, serializedParts, assemblyParts, hairIndex);
    result.fallbackLoadsNativeHair = result.nativeHairAvailable
        && !result.replacementAvailable && !result.nativeHairInitiallyLoaded;
    return result;
}
