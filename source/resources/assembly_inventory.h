#ifndef ASSEMBLY_INVENTORY_H
#define ASSEMBLY_INVENTORY_H

#include <QStringList>

namespace ei {

// Reads only RES indexes plus the model's LNK payload. Sources must already be
// ordered by active-profile precedence. Results are cached by source
// fingerprint and model name.
QStringList assemblyPartNamesFromResourceSources(
    const QStringList& resourceSources,
    const QString& resourceFingerprint,
    const QString& modelName);

}

#endif // ASSEMBLY_INVENTORY_H
