#ifndef CHARACTER_BODYPART_RUNTIME_H
#define CHARACTER_BODYPART_RUNTIME_H

#include <QString>
#include <QStringList>

namespace ei {

bool standardWearableAssembly(const QString& model);
// Fresh DB-created actors only. Existing serialized masks are never repaired
// or normalized by this helper; integral creatures retain their empty mask.
QStringList freshDatabaseBodyParts(const QString& model, int hairIndex);

struct SHelmetHairRuntimeDecision
{
    QString nativeHairPart;
    QString helmetPart;
    QString replacementPart;
    QString requiredSerializedPart;
    bool nativeHairAvailable{false};
    bool helmetPartAvailable{false};
    bool replacementAvailable{false};
    bool nativeHairInitiallyLoaded{false};
    bool fallbackLoadsNativeHair{false};
};

QString databaseHairPart(int hairIndex);
bool legacyHairRootAlias(const QString& model);
QStringList serializedAssemblyHairParts(const QStringList& serializedParts,
                                        const QStringList& assemblyParts);
bool serializedPartsLoadDatabaseHair(const QString& model,
                                     const QStringList& serializedParts,
                                     const QStringList& assemblyParts,
                                     int hairIndex);
SHelmetHairRuntimeDecision helmetHairRuntimeDecision(
    const QString& model,
    const QStringList& serializedParts,
    const QStringList& assemblyParts,
    int hairIndex,
    int helmetGeometry);

}

#endif
