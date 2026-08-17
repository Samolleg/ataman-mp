#ifndef SETTINGS_PERSISTENCE_H
#define SETTINGS_PERSISTENCE_H

#include <QByteArray>
#include <QString>

namespace ei {

bool writeSettingsFileAtomically(const QString& path, const QByteArray& data,
                                 QString* diagnostic = nullptr);

}

#endif // SETTINGS_PERSISTENCE_H
