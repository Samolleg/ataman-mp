#ifndef UNIT_APPEARANCE_H
#define UNIT_APPEARANCE_H

#include <QByteArray>
#include <QMap>
#include <QString>

namespace ei {

QMap<QString, int> unitPrototypeSkinIndices(const QByteArray& unitsDatabase);
QString humanoidTextureResourceName(const QMap<QString, int>& skinIndices,
                                    const QString& prototypeName,
                                    const QString& modelName);

}

#endif // UNIT_APPEARANCE_H
