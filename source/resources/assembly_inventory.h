#ifndef ASSEMBLY_INVENTORY_H
#define ASSEMBLY_INVENTORY_H

#include <QStringList>

namespace ei {

enum class EHairCrownGeometry
{
    Unavailable,
    AtOrBelowHeadCrown,
    AboveHeadCrown
};

// Reads only RES indexes plus the model's LNK payload. Sources must already be
// ordered by active-profile precedence. Results are cached by source
// fingerprint and model name.
QStringList assemblyPartNamesFromResourceSources(
    const QStringList& resourceSources,
    const QString& resourceFingerprint,
    const QString& modelName);

// Evaluates the same eight-way FIG/BON interpolation used by the character
// renderer at one serialized complexion. The result describes only the
// authored geometry: it deliberately does not infer a role from an hr token.
EHairCrownGeometry assemblyHairCrownGeometryFromResourceSources(
    const QStringList& resourceSources,
    const QString& resourceFingerprint,
    const QString& modelName,
    const QString& hairPart,
    float complexionX,
    float complexionY,
    float complexionZ);

QString assemblyHairGeometryKey(const QString& modelName,
                                const QString& hairPart);

}

#endif // ASSEMBLY_INVENTORY_H
